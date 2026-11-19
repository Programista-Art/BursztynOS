#pragma once
/*
 * Menedzer zadan - okno BursztynUI (Procesy / Wydajnosc).
 *
 * Dane pochodza wylacznie z BWS 103 (migawka jadra); zakonczenie wylacznie
 * przez BWS 104 z tozsamoscia (PID, generacja). Odswiezanie: zadanie okresowe
 * petli aplikacji co ~1 s (proces spi miedzy probkami - bez aktywnego
 * czekania); kazda probka to jedno wywolanie jadra do stalego bufora (brak
 * alokacji na odswiezenie).
 */
#include "model.h"

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

class DaneTabeli : public DostawcaDanychTabeli {
public:
    explicit DaneTabeli(Model& m) : model_(m) {}
    size_t liczba_wierszy() override { return model_.liczba(); }
    size_t liczba_kolumn() override { return 6; }
    const char* naglowek_kolumny(size_t k) override {
        static const char* const n[] = {"Nazwa", "PID", "Stan", "Procesor", "Pamięć", "Czas działania"};
        return k < 6 ? n[k] : "";
    }
    int szerokosc_kolumny(size_t k) override {
        static const int s[] = {220, 50, 200, 90, 110, 120};
        return k < 6 ? s[k] : 80;
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
            case 5: {
                const uint64_t teraz = model_.ostatni_czas_ns();
                if (w->pid == 0 || !teraz || w->start_ns > teraz || !w->start_ns) return "—";
                format_czasu(teraz - w->start_ns, b, cap); return b;
            }
            default: return "";
        }
    }
    void zazadaj_sortowania(size_t k, bool rosnaco) override {
        if (k > 4) k = 4;                       // czas dzialania: sortowanie jak po nazwie nie ma sensu - pamiec
        model_.ustaw_sortowanie(static_cast<Kolumna>(k), rosnaco);
        if (po_sortowaniu) po_sortowaniu(kontekst);
    }
    void (*po_sortowaniu)(void*) = nullptr;
    void* kontekst = nullptr;
private:
    Model& model_;
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
        szczegoly_ = &procesy->dodaj<Etykieta>(Dokowanie::Dol, "Wybierz proces, aby zobaczyć szczegóły.");
        szczegoly_->ustaw_preferowany_rozmiar(Rozmiar(-1, 24));
        tabela_ = &procesy->dodaj<TabelaWirtualna>(Dokowanie::Wypelnij);
        tabela_->ustaw_dostawce(&dane_);
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

        // --- Wydajnosc
        auto* wyd = new PanelDokowany(); wyd->ustaw_wypelnienie(Marginesy(4)); wyd->ustaw_odstep(6);
        auto& siatka = wyd->dodaj<Siatka>(Dokowanie::Wypelnij);
        siatka.dodaj_wiersz(); siatka.dodaj_wiersz(DefinicjaWymiaru::piksele(150));
        siatka.dodaj_kolumne(); siatka.dodaj_kolumne();
        wykres_cpu_ = new Wykres(TypWykresu::Linia); wykres_ram_ = new Wykres(TypWykresu::Linia);
        Wykres* wykresy[] = {wykres_cpu_, wykres_ram_};
        const char* tytuly[] = {"Procesor - ostatnie 60 s (%)", "Pamięć RAM w użyciu - ostatnie 60 s (%)"};
        for (int i = 0; i < 2; ++i) {
            wykresy[i]->ustaw_tytul(tytuly[i]); wykresy[i]->ustaw_auto_skalowanie(false); wykresy[i]->ustaw_zakres(0, 100);
            siatka.dodaj_do_siatki(wykresy[i], 0, i);
        }
        info_cpu_ = new Etykieta(""); info_ram_ = new Etykieta("");
        info_cpu_->ustaw_zawijanie(true); info_ram_->ustaw_zawijanie(true);
        // Tekst trafia do etykiet dopiero po ukladzie (ustaw_tekst nie przelicza ukladu): wypelniaja caly wiersz siatki,
        // inaczej mialyby wysokosc pustego tekstu (samo wypelnienie) i opis bylby niewidoczny do zmiany rozmiaru okna.
        info_cpu_->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        info_ram_->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        siatka.dodaj_do_siatki(info_cpu_, 1, 0); siatka.dodaj_do_siatki(info_ram_, 1, 1);
        karty.dodaj_karte("Wydajność", wyd);
        karty.ustaw_aktywna_karte(0);
        // Po przelaczeniu karty od razu pokaz aktualne dane (ukryta karta nie jest odswiezana co sekunde).
        karty.przy_zmianie_karty([](Karty*, int, void* p) { static_cast<OknoMenedzera*>(p)->aktualizuj_widoczna_karte(); }, this);
        odswiez();
    }

    /* Jedna probka: BWS 103 do stalego bufora, model, widok. Wolane co ~1 s z petli aplikacji. */
    void odswiez() {
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

    void aktualizuj_wydajnosc() {
        int cpu[HISTORIA], ram[HISTORIA];
        const size_t n = model_.historia(cpu, ram, HISTORIA);
        for (size_t i = 0; i < n; ++i) { cpu[i] = (cpu[i] + 5) / 10; ram[i] = (ram[i] + 5) / 10; }
        wykres_cpu_->ustaw_punkty(0, cpu, n, "Procesor", motyw_kolor(MOTYW_KOLOR_AKCENT));
        wykres_ram_->ustaw_punkty(0, ram, n, "Pamięć");
        const System& s = model_.system();
        char t[400], v[32]; Tekst x(t, sizeof(t));
        format_procentu(s.cpu_promile, v, sizeof(v)); x.dodaj("Użycie procesora: "); x.dodaj(v);
        format_procentu(s.jadro_promile, v, sizeof(v)); x.dodaj("\nPraca jądra (PID 0, bez bezczynności): "); x.dodaj(v);
        format_procentu(s.bezczynnosc_promile, v, sizeof(v)); x.dodaj("\nBezczynność (HLT): "); x.dodaj(v);
        x.dodaj("\nŹródło: ");
        if (s.hpet) x.dodaj("HPET - czas między przełączeniami kontekstu (przerwania liczone do bieżącego procesu)");
        else if (s.ticki) { x.dodaj("tick schedulera co "); x.liczba(s.okres_ticku_ns / 1000000); x.dodaj(" ms (próbkowanie)"); }
        else x.dodaj("niedostępne");
        info_cpu_->ustaw_tekst(t);
        Tekst y(t, sizeof(t));
        if (!s.ram_dostepna) y.dodaj("Liczniki pamięci niedostępne.");
        else {
            format_pamieci(s.ram_zarzadzana, v, sizeof(v)); y.dodaj("RAM zarządzana przez system: "); y.dodaj(v);
            format_pamieci(s.ram_uzywana, v, sizeof(v)); y.dodaj("\nW użyciu: "); y.dodaj(v);
            format_pamieci(s.ram_wolna, v, sizeof(v)); y.dodaj("   Dostępna: "); y.dodaj(v);
            format_pamieci(s.ram_procesow, v, sizeof(v)); y.dodaj("\nPrzypisana procesom: "); y.dodaj(v);
            format_pamieci(s.ram_reszta, v, sizeof(v)); y.dodaj("\nJądro, bufory i pamięć współdzielona: "); y.dodaj(v);
            if (s.ram_firmware) { format_pamieci(s.ram_firmware, v, sizeof(v)); y.dodaj("\nRAM zgłoszona przez firmware: "); y.dodaj(v); }
        }
        info_ram_->ustaw_tekst(y.b);
    }

    void aktualizuj_szczegoly() {
        const Wiersz* w = model_.wybrany();
        static_assert(MAKS_TEKSTU_ETYKIETY <= Kontrolka::MAKS_DLUGOSC_TEKSTU, "tekst szczegolow miesci sie w etykiecie");
        char t[MAKS_TEKSTU_ETYKIETY], p[MAKS_TEKSTU_ETYKIETY], v[32]; Tekst x(t, sizeof(t));
        if (!w) {
            szczegoly_->ustaw_tekst("Wybierz proces, aby zobaczyć szczegóły."); pamiec_procesu_->ustaw_tekst("");
            stan_->ustaw_tekst(""); return;
        }
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
    PasekInformacyjny* baner_{};
    static constexpr size_t MAKS_TEKSTU_ETYKIETY = 256;
    Etykieta *cpu_{}, *ram_{}, *szczegoly_{}, *pamiec_procesu_{}, *stan_{}, *info_cpu_{}, *info_ram_{};
    TabelaWirtualna* tabela_{};
    Karty* karty_{};
    Przycisk *zakoncz_btn_{}, *odswiez_btn_{};
    Wykres *wykres_cpu_{}, *wykres_ram_{};
    OknoDialogowe *dlg_pytanie_{}, *dlg_info_{};
    bool synchronizacja_ = false, zamknij_siebie_ = false;
    int trwaly_ = 0;
    uint64_t probki_ = 0, bledy_ = 0, ostatni_wynik_ = 0;
    char ostatni_komunikat_[200]{};
};

}  // namespace MenedzerZadan
