#pragma once
/*
 * Menedzer zadan - okno BursztynUI (Procesy / Wydajnosc: procesor, pamiec, dyski, siec, grafika).
 *
 * Dane pochodza wylacznie z BWS 103/105/106 (migawki jadra); zakonczenie wylacznie
 * przez BWS 104 z tozsamoscia (PID, generacja). Odswiezanie: zadanie okresowe
 * petli aplikacji co ~1 s (proces spi miedzy probkami - bez aktywnego
 * czekania); kazda probka to jedno wywolanie jadra do stalego bufora (brak
 * alokacji na odswiezenie).
 */
#include "model.h"
#include "urzadzenia.h"
#include "wydajnosc.h"

namespace MenedzerZadan {

using namespace BursztynUI;

/* Staly bufor odpowiedzi BWS 103 (naglowek + maksymalna liczba wpisow). */
struct BuforMigawki {
    BwsProcesyNaglowek naglowek;
    BwsProcesInfoV2 wpisy[BWS_PROCESY_V2_MAKS];
};

inline const char* opis_bledu_migawki(uint64_t w) {
    switch (w) {
        case BWS_MIGAWKA_BRAK_UPRAWNIEN: return "Brak uprawnienia do odczytu listy procesów (PRAWO_ZARZADZANIE_PROCESAMI).";
        case BWS_MIGAWKA_ZLY_BUFOR: return "Jądro odrzuciło bufor telemetrii.";
        case BWS_MIGAWKA_ZLA_WERSJA: return "Niezgodna wersja interfejsu telemetrii procesów.";
        default: return "Telemetria procesów jest chwilowo niedostępna.";
    }
}

/* Tempo I/O w kolumnie: "—" gdy licznik nie dotyczy procesu, "Niedostępne" gdy nie ma jeszcze przedzialu. */
inline const char* komorka_tempa(int64_t bps, bool dotyczy, char* b, size_t cap) {
    if (!dotyczy) return "—";
    if (bps < 0) return "Niedostępne";
    format_tempa(static_cast<uint64_t>(bps), b, cap);
    return b;
}

class DaneTabeli : public DostawcaDanychTabeli {
public:
    static constexpr size_t KOLUMN = 8;
    explicit DaneTabeli(Model& m) : model_(m) {}
    size_t liczba_wierszy() override { return model_.liczba(); }
    size_t liczba_kolumn() override { return KOLUMN; }
    const char* naglowek_kolumny(size_t k) override {
        static const char* const n[KOLUMN] = {"Nazwa", "PID", "Stan", "Procesor", "Pamięć", "Dysk", "Sieć", "Czas działania"};
        return k < KOLUMN ? n[k] : "";
    }
    int szerokosc_kolumny(size_t k) override {
        // Suma 950 DIP: przy oknie 1280 px miesci sie w calosci, przy mniejszym tabela przewija sie w poziomie.
        static const int s[KOLUMN] = {210, 50, 170, 90, 100, 105, 105, 120};
        return k < KOLUMN ? s[k] : 80;
    }
    const char* pobierz_komorke(size_t r, size_t k, char* b, size_t cap) override {
        const Wiersz* w = model_.wiersz(static_cast<uint32_t>(r));
        if (!w || !b || !cap) return "";
        Tekst t(b, cap);
        switch (k) {
            case 0: return w->nazwa;
            case 1: t.liczba(w->pid); return b;
            case 2: return nazwa_stanu(w->stan, w->wywolujacy());
            case 3: format_procentu(w->cpu_promile, b, cap); return b;
            case 4:
                if (w->pid == 0 || (!w->pamiec_zmierzona() && !w->okno)) return "Niedostępne";
                format_pamieci(w->pamiec(), b, cap); return b;
            case 5: return komorka_tempa(w->dysk_bps, w->pid != 0, b, cap);   // jadro nie uzywa wywolan plikowych BWS
            case 6: return komorka_tempa(w->siec_bps, true, b, cap);
            case 7: {
                const uint64_t teraz = model_.ostatni_czas_ns();
                if (w->pid == 0 || !teraz || w->start_ns > teraz || !w->start_ns) return "—";
                format_czasu(teraz - w->start_ns, b, cap); return b;
            }
            default: return "";
        }
    }
    void zazadaj_sortowania(size_t k, bool rosnaco) override {
        if (k >= KOLUMN) k = 0;
        model_.ustaw_sortowanie(static_cast<Kolumna>(k), rosnaco);
        if (po_sortowaniu) po_sortowaniu(kontekst);
    }
    void (*po_sortowaniu)(void*) = nullptr;
    void* kontekst = nullptr;
private:
    Model& model_;
};

/* Tabela "Wlasciwosc / Wartosc" szczegolow wybranej kategorii karty Wydajnosc (bez alokacji, tekst z modelu). */
class DaneSzczegolow : public DostawcaDanychTabeli {
public:
    size_t liczba_wierszy() override { return dane.n; }
    size_t liczba_kolumn() override { return 2; }
    const char* naglowek_kolumny(size_t k) override { return k == 0 ? "Właściwość" : k == 1 ? "Wartość" : ""; }
    int szerokosc_kolumny(size_t k) override { return k == 0 ? 220 : 430; }
    const char* pobierz_komorke(size_t r, size_t k, char*, size_t) override {
        if (r >= dane.n) return "";
        return k == 0 ? dane.klucz[r] : dane.wartosc[r];
    }
    Szczegoly dane;
};

class OknoMenedzera : public Okno {
public:
    OknoMenedzera() : Okno("Menedżer zadań", 900, 580), dane_(model_) {
        ustaw_minimalny_rozmiar(620, 420);
    }
    ~OknoMenedzera() override {
        zarzadca_popupow().zamknij(PowodZamknieciaPopup::WlascicielZamkniety);
        auto* p = kontrolka_glowna(); ustaw_kontrolke_glowna(nullptr); delete p;
        delete dlg_pytanie_; delete dlg_info_;
    }

    Model& model() { return model_; }

    /* Uzywane przez testy hosta: wywolanie jadra jest jedyna zaleznoscia od systemu. */
    uint64_t (*wywolaj)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) = nullptr;
    uint64_t (*zegar_ns)() = nullptr;

    void init() {
        auto& root = dodaj<PanelDokowany>();
        root.ustaw_wypelnienie(Marginesy(8)); root.ustaw_odstep(6);
        baner_ = &root.dodaj<PasekInformacyjny>(Dokowanie::Gora, TypKomunikatu::Informacja, "Odczyt procesów z jądra…");
        baner_->ustaw_przycisk_zamkniecia(false);
        auto& podsumowanie = root.dodaj<Siatka>(Dokowanie::Gora);
        podsumowanie.ustaw_preferowany_rozmiar(Rozmiar(-1, 28));
        podsumowanie.dodaj_wiersz(); podsumowanie.dodaj_kolumne(); podsumowanie.dodaj_kolumne();
        cpu_ = new Etykieta("Procesor: —"); ram_ = new Etykieta("Pamięć: —");
        podsumowanie.dodaj_do_siatki(cpu_, 0, 0); podsumowanie.dodaj_do_siatki(ram_, 0, 1);

        auto& karty = root.dodaj<Karty>(Dokowanie::Wypelnij);
        karty_ = &karty;
        // --- Procesy
        auto* procesy = new PanelDokowany(); procesy->ustaw_wypelnienie(Marginesy(0)); procesy->ustaw_odstep(6);
        auto& dol = procesy->dodaj<Siatka>(Dokowanie::Dol);
        dol.ustaw_preferowany_rozmiar(Rozmiar(-1, 34)); dol.dodaj_wiersz();
        dol.dodaj_kolumne(); dol.dodaj_kolumne(DefinicjaWymiaru::piksele(110)); dol.dodaj_kolumne(DefinicjaWymiaru::piksele(150));
        odswiez_btn_ = new Przycisk("Odśwież");
        zakoncz_btn_ = new Przycisk("Zakończ zadanie");
        stan_ = new Etykieta("");
        dol.dodaj_do_siatki(stan_, 0, 0); dol.dodaj_do_siatki(odswiez_btn_, 0, 1); dol.dodaj_do_siatki(zakoncz_btn_, 0, 2);
        odswiez_btn_->przy_kliknieciu([](Przycisk*, void* p) { static_cast<OknoMenedzera*>(p)->odswiez(); }, this);
        zakoncz_btn_->przy_kliknieciu([](Przycisk*, void* p) { static_cast<OknoMenedzera*>(p)->zakoncz_zadanie(); }, this);
        // Dwie etykiety, kazda ponizej limitu tekstu kontrolki (Kontrolka::MAKS_DLUGOSC_TEKSTU): obciety tekst nigdy nie
        // rownalby sie nowemu, wiec etykieta bylaby uniewazniana i ukladana od nowa przy kazdym odswiezeniu.
        pamiec_procesu_ = &procesy->dodaj<Etykieta>(Dokowanie::Dol, "");
        pamiec_procesu_->ustaw_zawijanie(true); pamiec_procesu_->ustaw_preferowany_rozmiar(Rozmiar(-1, 50));
        io_procesu_ = &procesy->dodaj<Etykieta>(Dokowanie::Dol, "");
        // 2 linie przy natywnej czcionce (~20 px) + wypelnienie: 40 DIP ucinalo linie "Sieć" (natywnie, 2026-11-19)
        io_procesu_->ustaw_zawijanie(true); io_procesu_->ustaw_preferowany_rozmiar(Rozmiar(-1, 52));
        szczegoly_ = &procesy->dodaj<Etykieta>(Dokowanie::Dol, "Wybierz proces, aby zobaczyć szczegóły.");
        szczegoly_->ustaw_preferowany_rozmiar(Rozmiar(-1, 24));
        tabela_ = &procesy->dodaj<TabelaWirtualna>(Dokowanie::Wypelnij);
        tabela_->ustaw_dostawce(&dane_);
        tabela_->ustaw_przewijanie_poziome(true);   // 8 kolumn nie miesci sie w minimalnym oknie
        tabela_->wyczysc_wybor();
        tabela_->przy_zmianie_wyboru([](TabelaWirtualna*, size_t r, void* p) {
            auto* o = static_cast<OknoMenedzera*>(p);
            if (o->synchronizacja_) return;      // programowe odtworzenie zaznaczenia po odswiezeniu
            o->model_.wybierz_indeks(static_cast<uint32_t>(r));
            o->aktualizuj_szczegoly();
        }, this);
        dane_.po_sortowaniu = [](void* p) { static_cast<OknoMenedzera*>(p)->odtworz_wybor(); };
        dane_.kontekst = this;
        karty.dodaj_karte("Procesy", procesy);

        // --- Wydajnosc: lista kategorii (dynamicznie z migawek) po lewej, szczegoly wybranej po prawej.
        auto* wyd = new PanelDokowany(); wyd->ustaw_wypelnienie(Marginesy(4)); wyd->ustaw_odstep(8);
        lista_ = &wyd->dodaj<ListaWyboru>(Dokowanie::Lewo);
        lista_->ustaw_preferowany_rozmiar(Rozmiar(240, -1));
        lista_->ustaw_wysokosc_wiersza(42);
        lista_->ustaw_przewijanie_poziome(false);
        lista_->ustaw_rysowanie_elementu([](ListaWyboru* l, KontekstRysowania& c, int i, Prostokat r, bool z, bool f, void* p) {
            static_cast<OknoMenedzera*>(p)->rysuj_kategorie(*l, c, i, r, z, f);
        }, this);
        lista_->przy_zmianie_wyboru([](ListaWyboru* l, int i, void* p) {
            auto* o = static_cast<OknoMenedzera*>(p);
            if (o->synchronizacja_ || i < 0) return;
            o->widok_ = l->znacznik_elementu(static_cast<size_t>(i));
            o->pokaz_widok(true);
        }, this);
        auto* szcz = &wyd->dodaj<PanelDokowany>(Dokowanie::Wypelnij);
        szcz->ustaw_wypelnienie(Marginesy(0)); szcz->ustaw_odstep(6);
        naglowek_widoku_ = &szcz->dodaj<Etykieta>(Dokowanie::Gora, "Procesor");
        naglowek_widoku_->ustaw_autodopasowanie(false); naglowek_widoku_->ustaw_preferowany_rozmiar(Rozmiar(-1, 26));
        tabela_szcz_ = &szcz->dodaj<TabelaWirtualna>(Dokowanie::Dol);
        tabela_szcz_->ustaw_preferowany_rozmiar(Rozmiar(-1, 172));
        tabela_szcz_->ustaw_przewijanie_poziome(true);
        tabela_szcz_->ustaw_dostawce(&dane_szcz_);
        tabela_szcz_->wyczysc_wybor();
        wykres_ = &szcz->dodaj<Wykres>(Dokowanie::Wypelnij, TypWykresu::Linia);
        karty.dodaj_karte("Wydajność", wyd);
        karty.ustaw_aktywna_karte(0);
        // Po przelaczeniu karty od razu pokaz aktualne dane (ukryta karta nie jest odswiezana co sekunde).
        karty.przy_zmianie_karty([](Karty*, int, void* p) { static_cast<OknoMenedzera*>(p)->aktualizuj_widoczna_karte(); }, this);
        odswiez();
    }

    /* Jedna probka: BWS 103 do stalego bufora, model, widok. Wolane co ~1 s z petli aplikacji. */
    void odswiez() {
        // Liczniki I/O przed migawka procesow: zastosuj() wpisuje tempa do wierszy przed sortowaniem.
        bufor_io_.naglowek = BwsProcesyIoNaglowek{};
        const uint64_t wio = wywolaj ? wywolaj(BWS_PROCESY_IO, reinterpret_cast<uint64_t>(&bufor_io_), sizeof(bufor_io_),
                                               BWS_PROCESY_IO_V1_ABI, 0) : 0;
        if (wio != BWS_TELEMETRIA_OK || !model_.zastosuj_io(bufor_io_)) { model_.blad_io(); ++bledy_io_; }
        // Urzadzenia probkowane zawsze (jedno wywolanie): historia wykresow trwa takze przy ukrytej karcie.
        bufor_urz_.naglowek = BwsUrzadzeniaNaglowek{};
        const uint64_t wu = wywolaj ? wywolaj(BWS_URZADZENIA_TELEMETRIA, reinterpret_cast<uint64_t>(&bufor_urz_),
                                              sizeof(bufor_urz_), BWS_URZADZENIA_V1_ABI, 0) : 0;
        if (wu != BWS_TELEMETRIA_OK || !urzadzenia_.zastosuj(bufor_urz_)) { if (wu != BWS_TELEMETRIA_OK) urzadzenia_.blad(); }
        bufor_.naglowek = BwsProcesyNaglowek{};
        const uint64_t w = wywolaj ? wywolaj(BWS_PROCESY_MIGAWKA, reinterpret_cast<uint64_t>(&bufor_), sizeof(bufor_),
                                             BWS_PROCESY_V2_ABI, 0) : 0;
        const uint64_t teraz = zegar_ns ? zegar_ns() : 0;
        if (w != BWS_MIGAWKA_OK || !model_.zastosuj(bufor_.naglowek, bufor_.wpisy, teraz)) {
            model_.blad_odczytu();
            komunikat(w == BWS_MIGAWKA_OK ? "Jądro zwróciło niespójną migawkę procesów - pominięto." : opis_bledu_migawki(w),
                      TypKomunikatu::Blad);
            ++bledy_;
            return;
        }
        ++probki_;
        const Zdarzenie zdarzenie = model_.zdarzenie();
        switch (zdarzenie) {
            case Zdarzenie::Zakonczony: {
                char t[160]; Tekst x(t, sizeof(t)); x.dodaj("Proces „"); x.dodaj(model_.cel_nazwa());
                x.dodaj("” zakończył się, a jądro zwolniło jego zasoby."); komunikat(t, TypKomunikatu::Sukces); break;
            }
            case Zdarzenie::NadalKonczony: komunikat("Kończenie procesu… jądro zwalnia jego zasoby.", TypKomunikatu::Informacja); break;
            case Zdarzenie::PrzekroczonyCzas:
                komunikat("Proces nadal widnieje jako kończący po 10 s - zasoby nie zostały jeszcze zwolnione.", TypKomunikatu::Ostrzezenie); break;
            case Zdarzenie::Nic:
                if (trwaly_ > 0) --trwaly_; else komunikat_stanu(); break;
        }
        // Komunikat o zakonczeniu (z potwierdzonym zwolnieniem zasobow) ma pierwszenstwo przed informacja o wyborze.
        if (model_.wybor_zniknal() && zdarzenie != Zdarzenie::Zakonczony)
            komunikat("Wybrany proces zakończył się - zaznaczenie usunięto.", TypKomunikatu::Informacja);
        odtworz_wybor();
        aktualizuj_podsumowanie();
        aktualizuj_widoczna_karte();
    }

    /* Odrysowywana jest tylko widoczna karta: wykresy i opisy Wydajnosci nie kosztuja nic, gdy widac Procesy. */
    void aktualizuj_widoczna_karte() {
        if (karty_ && karty_->aktywna_karta() == 1) aktualizuj_wydajnosc();
        else tabela_->uniewaznij();
    }

    void odtworz_wybor() {
        synchronizacja_ = true;
        const int i = model_.indeks_wyboru();
        if (i >= 0) tabela_->ustaw_wybrany_wiersz(static_cast<size_t>(i));
        else tabela_->wyczysc_wybor();
        synchronizacja_ = false;
        aktualizuj_szczegoly();
    }

    void zakoncz_zadanie() {
        switch (model_.zadaj_zakonczenia()) {
            case Prosba::BrakWyboru: komunikat("Najpierw wybierz proces z listy.", TypKomunikatu::Informacja); return;
            case Prosba::Chroniony: {
                const Wiersz* w = model_.wybrany();
                pokaz_info("Nie można zakończyć procesu", w ? powod_ochrony(w->ochrona) : "Proces chroniony.", TypDialogu::Ostrzezenie);
                return;
            }
            case Prosba::ZamknijSiebie:
                pytanie("Menedżer zadań", "To okno Menedżera zadań. Czy zamknąć Menedżer zadań w zwykły sposób?", true);
                return;
            case Prosba::Potwierdz: {
                char t[320]; Tekst x(t, sizeof(t));
                x.dodaj("Czy na pewno chcesz zakończyć proces ");
                x.dodaj(model_.cel_nazwa()); x.dodaj(" (PID "); x.liczba(model_.cel().pid); x.dodaj(")?\n");
                x.dodaj("Niezapisane zmiany w tej aplikacji mogą zostać utracone.");
                pytanie("Zakończ zadanie", t, false);
                return;
            }
        }
    }

    // ---- stan dla testow i diagnostyki
    uint64_t probki() const { return probki_; }
    uint64_t bledy() const { return bledy_; }
    TabelaWirtualna* tabela() { return tabela_; }
    Przycisk* przycisk_zakoncz() { return zakoncz_btn_; }
    Etykieta* etykieta_szczegolow() { return szczegoly_; }
    Etykieta* etykieta_pamieci() { return pamiec_procesu_; }
    Etykieta* etykieta_io() { return io_procesu_; }
    const ModelUrzadzen& urzadzenia() const { return urzadzenia_; }
    uint64_t bledy_io() const { return bledy_io_; }
    Etykieta* etykieta_stanu() { return stan_; }
    OknoDialogowe* dialog_pytanie() { return dlg_pytanie_; }
    OknoDialogowe* dialog_info() { return dlg_info_; }
    const char* tekst_banera() const { return ostatni_komunikat_; }
    uint64_t ostatni_wynik_zakonczenia() const { return ostatni_wynik_; }

private:
    void komunikat(const char* s, TypKomunikatu typ) {
        Tekst t(ostatni_komunikat_, sizeof(ostatni_komunikat_)); t.dodaj(s);
        baner_->ustaw_tekst(s); baner_->ustaw_typ_komunikatu(typ);
        trwaly_ = 5;                                   // komunikat zostaje ~5 s, potem wraca opis stanu
    }
    void komunikat_stanu() {
        const System& s = model_.system();
        char t[200]; Tekst x(t, sizeof(t));
        x.dodaj("Procesy: "); x.liczba(s.procesow);
        if (s.obcieta) x.dodaj(" (lista obcięta)");
        x.dodaj(s.hpet ? "   Pomiar CPU: zegar HPET, odcinki między przełączeniami kontekstu"
                       : s.ticki ? "   Pomiar CPU: próbkowanie tickiem schedulera (bez HPET)"
                                 : "   Pomiar CPU: niedostępny");
        baner_->ustaw_tekst(t); baner_->ustaw_typ_komunikatu(TypKomunikatu::Informacja);
    }

    void aktualizuj_podsumowanie() {
        const System& s = model_.system();
        char t[160], v[32], w[32];
        { Tekst x(t, sizeof(t)); format_procentu(s.cpu_promile, v, sizeof(v)); x.dodaj("Procesor: "); x.dodaj(v);
          if (!s.cpu_dostepne) x.dodaj(" (zbieranie pierwszego przedziału)");
          cpu_->ustaw_tekst(t); }
        { Tekst x(t, sizeof(t)); x.dodaj("Pamięć: ");
          if (!s.ram_dostepna) x.dodaj("Niedostępne");
          else { format_pamieci(s.ram_uzywana, v, sizeof(v)); format_pamieci(s.ram_zarzadzana, w, sizeof(w));
                 x.dodaj(v); x.dodaj(" z "); x.dodaj(w); x.dodaj(" w użyciu (");
                 format_procentu(s.ram_promile, v, sizeof(v)); x.dodaj(v); x.dodaj(")"); }
          ram_->ustaw_tekst(t); }
        zakoncz_btn_->ustaw_wlaczona(model_.etap() != Etap::Potwierdzanie);
    }

    /*
     * Karta Wydajnosc (tylko gdy widoczna). Koszt staly i maly: lista kategorii jest przebudowywana wylacznie po
     * zmianie zestawu urzadzen, wiersz listy przerysowuje sie tylko, gdy zmienila sie jego wartosc, wykres tylko,
     * gdy zmienily sie punkty (Wykres::ustaw_punkty), a tabela szczegolow tylko, gdy zmienil sie jej tekst.
     * Historie wszystkich kategorii zbiera model niezaleznie od widocznosci (bez rysowania).
     */
    void aktualizuj_wydajnosc() {
        const Kategorie k = kategorie(urzadzenia_);
        if (!(k == kategorie_)) przebuduj_liste(k);
        const System& s = model_.system();
        for (size_t i = 0; i < kategorie_.n; ++i) {
            char v[64]; wartosc_kategorii(kategorie_.z[i], s, urzadzenia_, v, sizeof(v));
            if (!rowne(v, wartosci_[i])) { Tekst t(wartosci_[i], sizeof(wartosci_[i])); t.dodaj(v); lista_->odswiez_element(int(i)); }
        }
        pokaz_widok(false);
    }

    /* Wybrana kategoria: naglowek, seria wykresu i szczegoly. przelaczenie = zmiana kategorii (nowe serie i skala). */
    void pokaz_widok(bool przelaczenie) {
        if (kategorie_.indeks(widok_) < 0) { widok_ = znacznik_widoku(Widok::Procesor); przelaczenie = true; }
        const Widok rodzaj = rodzaj_widoku(widok_);
        if (przelaczenie || widok_ != pokazany_) {
            pokazany_ = widok_;
            char t[64]; nazwa_kategorii(widok_, urzadzenia_, t, sizeof(t));
            naglowek_widoku_->ustaw_tekst(t);
            wykres_->wyczysc();
            switch (rodzaj) {
                case Widok::Procesor: wykres_->ustaw_tytul("Użycie procesora - ostatnie 60 s (%)"); wykres_->ustaw_zakres(0, 100); break;
                case Widok::Pamiec: wykres_->ustaw_tytul("Pamięć RAM w użyciu - ostatnie 60 s (%)"); wykres_->ustaw_zakres(0, 100); break;
                case Widok::Dysk: wykres_->ustaw_tytul("Odczyt i zapis (KiB/s)"); wykres_->ustaw_auto_skalowanie(true); break;
                case Widok::Siec: wykres_->ustaw_tytul("Odbiór i wysyłanie (KiB/s)"); wykres_->ustaw_auto_skalowanie(true); break;
                case Widok::Grafika: wykres_->ustaw_tytul("Klatki kompozytora na sekundę"); wykres_->ustaw_auto_skalowanie(true); break;
            }
            synchronizacja_ = true;
            const int i = kategorie_.indeks(widok_);
            if (lista_->wybrany_indeks() != i) lista_->ustaw_wybrany_indeks(i);
            synchronizacja_ = false;
        }
        int a[HISTORIA], b[HISTORIA];
        size_t na = 0, nb = 0;
        const uint32_t akcent = motyw_kolor(MOTYW_KOLOR_AKCENT);
        Szczegoly nowe;
        switch (rodzaj) {
            case Widok::Procesor: case Widok::Pamiec: {
                na = model_.historia(rodzaj == Widok::Procesor ? a : nullptr, rodzaj == Widok::Pamiec ? a : nullptr, HISTORIA);
                for (size_t i = 0; i < na; ++i) a[i] = (a[i] + 5) / 10;
                wykres_->ustaw_punkty(0, a, na, rodzaj == Widok::Procesor ? "Procesor" : "Pamięć", akcent);
                if (rodzaj == Widok::Procesor) szczegoly_procesora(model_.system(), model_.ostatni_czas_ns(), nowe);
                else szczegoly_pamieci(model_.system(), nowe);
                break;
            }
            case Widok::Dysk: {
                const Dysk* d = urzadzenia_.znajdz_dysk(id_widoku(widok_));
                if (!d) break;
                na = d->h_odczyt.kopiuj(a, HISTORIA); nb = d->h_zapis.kopiuj(b, HISTORIA);
                wykres_->ustaw_punkty(0, a, na, "Odczyt", akcent); wykres_->ustaw_punkty(1, b, nb, "Zapis");
                szczegoly_dysku(*d, urzadzenia_.psf_w_ram(), nowe);
                break;
            }
            case Widok::Siec: {
                const Siec* n = urzadzenia_.znajdz_siec(id_widoku(widok_));
                if (!n) break;
                na = n->h_rx.kopiuj(a, HISTORIA); nb = n->h_tx.kopiuj(b, HISTORIA);
                wykres_->ustaw_punkty(0, a, na, "Odbiór", akcent); wykres_->ustaw_punkty(1, b, nb, "Wysyłanie");
                szczegoly_sieci(*n, nowe);
                break;
            }
            case Widok::Grafika: {
                const Grafika* g = urzadzenia_.grafika();
                if (!g) break;
                na = g->h_klatki.kopiuj(a, HISTORIA);
                wykres_->ustaw_punkty(0, a, na, "Klatki", akcent);
                szczegoly_grafiki(*g, nowe);
                break;
            }
        }
        if (!(nowe == dane_szcz_.dane)) { dane_szcz_.dane = nowe; tabela_szcz_->uniewaznij(); }
    }

    /* Elementy listy alokuja tekst tylko tutaj - po zmianie zestawu urzadzen (rzadko), nie co sekunde. */
    void przebuduj_liste(const Kategorie& k) {
        synchronizacja_ = true;
        lista_->wyczysc();
        for (size_t i = 0; i < k.n; ++i) {
            char t[64]; nazwa_kategorii(k.z[i], urzadzenia_, t, sizeof(t));
            lista_->dodaj_element(t, k.z[i]);
            wartosci_[i][0] = '\0';
        }
        kategorie_ = k;
        synchronizacja_ = false;
        pokazany_ = ~0ULL;          // wymus ustawienie serii i wyboru dla biezacej kategorii
    }

    void rysuj_kategorie(ListaWyboru& l, KontekstRysowania& c, int i, Prostokat r, bool zaznaczony, bool fokus) {
        const SkalaUI sk = l.skala_dpi();
        if (zaznaczony) c.rysuj_prostokat(r, motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE));
        else if (fokus) c.rysuj_prostokat(r, motyw_kolor(MOTYW_KOLOR_PRZYCISK_NAJECHANY));
        // pasek akcentu: wyroznienie wybranej kategorii w barwach Bursztyna
        if (zaznaczony) c.rysuj_prostokat(Prostokat(r.x, r.y + 4 * sk, 3 * sk, r.wysokosc - 8 * sk), motyw_kolor(MOTYW_KOLOR_AKCENT));
        const int h = CzcionkaUI::pobierz_wysokosc(sk);
        const uint32_t k1 = zaznaczony ? motyw_kolor_tekstu_zaznaczenia() : motyw_kolor(MOTYW_KOLOR_TEKST);
        const uint32_t k2 = zaznaczony ? motyw_kolor_tekstu_zaznaczenia() : motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI);
        const int x = r.x + 10 * sk, w = r.szerokosc - 14 * sk, y = r.y + (r.wysokosc - 2 * h) / 2;
        const char* nazwa = l.element(static_cast<size_t>(i));
        c.rysuj_tekst_obciety(Punkt(x, y), nazwa ? nazwa : "", k1, w, sk);
        if (i >= 0 && static_cast<size_t>(i) < kategorie_.n) c.rysuj_tekst_obciety(Punkt(x, y + h), wartosci_[i], k2, w, sk);
    }

    void aktualizuj_szczegoly() {
        const Wiersz* w = model_.wybrany();
        static_assert(MAKS_TEKSTU_ETYKIETY <= Kontrolka::MAKS_DLUGOSC_TEKSTU, "tekst szczegolow miesci sie w etykiecie");
        char t[MAKS_TEKSTU_ETYKIETY], p[MAKS_TEKSTU_ETYKIETY], v[32]; Tekst x(t, sizeof(t));
        if (!w) {
            szczegoly_->ustaw_tekst("Wybierz proces, aby zobaczyć szczegóły."); pamiec_procesu_->ustaw_tekst("");
            io_procesu_->ustaw_tekst(""); stan_->ustaw_tekst(""); return;
        }
        aktualizuj_io(*w);
        x.dodaj(w->nazwa); x.dodaj("  ·  PID "); x.liczba(w->pid);
        if (w->pid) { x.dodaj(", generacja "); x.liczba(w->generacja); }
        x.dodaj(", PZB "); x.liczba(w->poziom);
        if (w->sciezka[0]) { x.dodaj("  ·  "); x.dodaj(w->sciezka); }
        szczegoly_->ustaw_tekst(t);
        x = Tekst(p, sizeof(p));
        if (w->pid == 0) x.dodaj("Pamięć jądra nie jest przypisywana PID 0 (zob. Wydajność).");
        else if (w->pamiec_zmierzona()) {
            format_pamieci(w->prywatna, v, sizeof(v)); x.dodaj("Prywatna "); x.dodaj(v);
            format_pamieci(w->tablice, v, sizeof(v)); x.dodaj(", tablice stron "); x.dodaj(v);
            format_pamieci(w->stos, v, sizeof(v)); x.dodaj(", stos jądra "); x.dodaj(v);
            format_pamieci(w->okno, v, sizeof(v)); x.dodaj(", bufor okna "); x.dodaj(v);
            format_pamieci(w->wspoldzielona, v, sizeof(v)); x.dodaj("\nWspółdzielona "); x.dodaj(v); x.dodaj(" (niewliczana)");
            format_pamieci(w->sterta, v, sizeof(v)); x.dodaj("; sterta zarezerwowana (wirtualnie) "); x.dodaj(v);
            if (w->flagi & BWS_PROC_PAMIEC_NIEPELNA) x.dodaj(" - pomiar niepełny");
        } else x.dodaj("Pamięć procesu: niedostępna (proces w budowie albo kończony).");
        pamiec_procesu_->ustaw_tekst(p);
        stan_->ustaw_tekst(w->mozna_zakonczyc() ? "" : krotki_powod_ochrony(w->ochrona));
    }

    /* Skumulowane liczniki generacji i tempo; definicje: docs/menedzer-zadan-monitorowanie.md (etap 2). */
    void aktualizuj_io(const Wiersz& w) {
        char t[MAKS_TEKSTU_ETYKIETY], v[32], r[32]; Tekst x(t, sizeof(t));
        if (!w.io_znane) { io_procesu_->ustaw_tekst("Liczniki dysku i sieci: niedostępne (brak odpowiedzi jądra)."); return; }
        auto para = [&](uint64_t suma, int64_t bps) {
            format_ilosci(suma, v, sizeof(v)); x.dodaj(v);
            if (bps >= 0) { format_tempa(static_cast<uint64_t>(bps), r, sizeof(r)); x.dodaj(" ("); x.dodaj(r); x.dodaj(")"); }
        };
        if (w.pid == 0) x.dodaj("Pliki: nie dotyczy jądra");
        else {
            x.dodaj("Pliki (logicznie): odczyt "); para(w.pliki_odczyt, w.pliki_odczyt_bps);
            x.dodaj(", zapis "); para(w.pliki_zapis, w.pliki_zapis_bps);
        }
        x.dodaj("\nSieć (dane TCP): wysłano "); para(w.siec_wyslane, w.siec_wyslane_bps);
        x.dodaj(", odebrano "); para(w.siec_odebrane, w.siec_odebrane_bps);
        io_procesu_->ustaw_tekst(t);
    }

    OknoDialogowe* dialog(OknoDialogowe*& d, PrzyciskiDialogu przyciski, TypDialogu typ) {
        if (!d) d = new OknoDialogowe("", "", przyciski, typ);
        return d;
    }
    void pokaz_info(const char* tytul, const char* tresc, TypDialogu typ) {
        if (dlg_info_) { delete dlg_info_; dlg_info_ = nullptr; }      // typ ikony ustalany w konstruktorze
        dlg_info_ = new OknoDialogowe(tytul, tresc, PrzyciskiDialogu::TylkoOk, typ);
        dlg_info_->pokaz(zarzadca_popupow(), kontrolka_glowna());
    }
    void pytanie(const char* tytul, const char* tresc, bool zamknij_siebie) {
        OknoDialogowe* d = dialog(dlg_pytanie_, PrzyciskiDialogu::TakNie, TypDialogu::Ostrzezenie);
        zamknij_siebie_ = zamknij_siebie;
        d->ustaw_tekst(tytul); d->ustaw_wiadomosc(tresc);
        d->ustaw_przycisk_domyslny(WynikDialogu::Nie);           // Enter nie konczy procesu przypadkiem
        d->przy_wyniku([](OknoDialogowe*, WynikDialogu wynik, void* p) { static_cast<OknoMenedzera*>(p)->wynik_pytania(wynik); }, this);
        d->pokaz(zarzadca_popupow(), kontrolka_glowna());
        zakoncz_btn_->ustaw_wlaczona(zamknij_siebie);
    }
    void wynik_pytania(WynikDialogu wynik) {
        zakoncz_btn_->ustaw_wlaczona(true);
        if (zamknij_siebie_) { if (wynik == WynikDialogu::Tak) zamknij(); return; }   // zwykle zamkniecie okna
        if (wynik != WynikDialogu::Tak) { model_.anuluj(); komunikat("Anulowano - żaden proces nie został zakończony.", TypKomunikatu::Informacja); return; }
        auto* self = this;
        ostatni_wynik_ = model_.potwierdz([self](uint32_t pid, uint64_t gen) {
            return self->wywolaj ? self->wywolaj(BWS_PROCES_ZAKONCZ, pid, gen, 0, 0) : uint64_t(BWS_ZAKONCZ_BLAD);
        }, zegar_ns ? zegar_ns() : 0);
        const bool przyjete = ostatni_wynik_ == BWS_ZAKONCZ_ZAAKCEPTOWANO || ostatni_wynik_ == BWS_ZAKONCZ_JUZ_KONCZONY;
        komunikat(opis_wyniku_zakonczenia(ostatni_wynik_), przyjete ? TypKomunikatu::Informacja : TypKomunikatu::Blad);
        if (!przyjete) pokaz_info("Nie zakończono procesu", opis_wyniku_zakonczenia(ostatni_wynik_), TypDialogu::Blad);
        odswiez();
    }

    Model model_;
    DaneTabeli dane_;
    BuforMigawki bufor_{};
    BwsProcesyIoOdpowiedz bufor_io_{};
    BwsUrzadzeniaOdpowiedz bufor_urz_{};
    ModelUrzadzen urzadzenia_;
    PasekInformacyjny* baner_{};
    static constexpr size_t MAKS_TEKSTU_ETYKIETY = 256;
    Etykieta *cpu_{}, *ram_{}, *szczegoly_{}, *pamiec_procesu_{}, *io_procesu_{}, *stan_{}, *naglowek_widoku_{};
    TabelaWirtualna* tabela_{};
    Karty* karty_{};
    Przycisk *zakoncz_btn_{}, *odswiez_btn_{};
    Wykres* wykres_{};
    ListaWyboru* lista_{};
    TabelaWirtualna* tabela_szcz_{};
    DaneSzczegolow dane_szcz_;
    Kategorie kategorie_{};
    char wartosci_[MAKS_KATEGORII][64]{};
    uint64_t widok_ = znacznik_widoku(Widok::Procesor), pokazany_ = ~0ULL;
    OknoDialogowe *dlg_pytanie_{}, *dlg_info_{};
    bool synchronizacja_ = false, zamknij_siebie_ = false;
    int trwaly_ = 0;
    uint64_t probki_ = 0, bledy_ = 0, bledy_io_ = 0, ostatni_wynik_ = 0;
    char ostatni_komunikat_[200]{};
};

}  // namespace MenedzerZadan
