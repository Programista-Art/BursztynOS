#pragma once
/*
 * Grajek - kontroler aplikacji (bez GUI): silnik + biblioteka + kolejka + konfiguracja + okladka.
 *
 * Interfejs BursztynUI tylko wywoluje polecenia i odczytuje stan (liczniki zmian), dzieki czemu cala logika
 * przejsc (automatyczny nastepny utwor, powtarzanie, losowo, koniec listy, odswiezanie biblioteki, zapis ustawien,
 * odtwarzanie w tle) jest testowana na hoscie.
 *
 * Kolejnosc pracy w tick(): (1) okladka nowego utworu - dekodowana PRZED otwarciem strumienia PCM, wiec nie
 * powoduje niedoboru, (2) silnik (produkcja PCM), (3) zdarzenia silnika i przejscia kolejki, (4) jeden krok skanu
 * biblioteki (po uzupelnieniu kolejki PCM), (5) odlozony zapis konfiguracji (nie czesciej niz co 2 s),
 * (6) przy wlaczonej diagnostyce - zamkniecie 1-sekundowego okna pomiaru (pomiar.h) i tekst podsumowania.
 *
 * Komunikat o niedoborze opisuje WYLACZNIE licznik "starved" strumienia Grajka (silnik.h, StatystykiCiaglosci):
 * ile ms wyjscia HDA nie mialo danych Grajka, gdy byly oczekiwane - osobno dla biezacego utworu i od uruchomienia.
 * Cisza z ladowania, przewijania, pauzy, prerollu czy pominietych okresow transportu HDA nie jest w nim zawarta.
 */
#include "silnik.h"
#include "kolejka.h"
#include "konfiguracja.h"
#include "okladka.h"
#include "../../jadro/bws/bws_magazyn.h"

namespace Grajek {

constexpr const char* ID_APLIKACJI = "grajek";
constexpr const char* KLUCZ_KONFIGURACJI = "konfiguracja";
constexpr const char* KLUCZ_LISTY = "kolejka.m3u8";

struct Platforma {
    DostepDoPlikow pliki{};
    FunkcjaListowania listuj = nullptr;
    void* kontekst_listowania = nullptr;
    FunkcjaBws bws = nullptr;
    bool (*wczytaj_ustawienie)(const char* klucz, char* bufor, uint32_t pojemnosc) = nullptr;
    bool (*zapisz_ustawienie)(const char* klucz, const char* wartosc) = nullptr;
    uint64_t (*czas_ns)() = nullptr;
};

enum class RodzajKomunikatu : uint8_t { Informacja, Sukces, Ostrzezenie, Blad };

class Odtwarzacz {
public:
    static constexpr uint64_t ODSTEP_ZAPISU_NS = 2000000000ULL;
    static constexpr uint64_t ODSTEP_NIEDOBORU_NS = 5000000000ULL;
    static constexpr uint64_t OKNO_DIAGNOSTYKI_NS = 1000000000ULL;

    void konfiguruj(const Platforma& p) {
        p_ = p;
        silnik_.konfiguruj(p.pliki, p.bws);
        silnik_.ustaw_zegar(p.czas_ns);
        bib_.konfiguruj(p.pliki, p.listuj, p.kontekst_listowania);
    }

    // Start aplikacji: konfiguracja, glosnosc, korektor i pierwszy skan /dane/muzyka.
    void start() {
        char t[4096] = {};
        if (p_.wczytaj_ustawienie && p_.wczytaj_ustawienie(KLUCZ_KONFIGURACJI, t, sizeof(t) - 1)) konf_.wczytaj(t, dlugosc(t, sizeof(t)));
        zastosuj_konfiguracje();
        odswiez_biblioteke();
    }

    // ------------------------------------------------------------ stan dla interfejsu
    Silnik& silnik() { return silnik_; }
    const Silnik& silnik() const { return silnik_; }
    Biblioteka& biblioteka() { return bib_; }
    const Biblioteka& biblioteka() const { return bib_; }
    Kolejka& kolejka() { return kol_; }
    const Konfiguracja& konfiguracja() const { return konf_; }
    Okladka& okladka() { return okl_; }
    int biezacy_utwor() const { return biezacy_; }
    const Utwor* utwor_biezacy() const { return biezacy_ >= 0 ? bib_.utwor(uint32_t(biezacy_)) : nullptr; }
    const char* komunikat() const { return komunikat_; }
    RodzajKomunikatu rodzaj_komunikatu() const { return rodzaj_; }
    // Liczniki zmian - interfejs odswieza elementy tylko, gdy licznik sie zmienil.
    uint32_t wersja_biblioteki() const { return w_bib_; }
    uint32_t wersja_kolejki() const { return w_kol_; }
    uint32_t wersja_utworu() const { return w_utw_; }
    uint32_t wersja_komunikatu() const { return w_kom_; }
    uint32_t wersja_ustawien() const { return w_ust_; }
    uint32_t wersja_diagnostyki() const { return w_diag_; }
    const char* tekst_diagnostyki() const { return diag_; }

    // ------------------------------------------------------------ praca okresowa
    void tick() {
        if (silnik_.stan() == Stan::Ladowanie && okladka_do_wczytania_) {
            okladka_do_wczytania_ = false;
            const Utwor* u = utwor_biezacy();
            PomiarSekcji pom(Sekcja::Okladka);
            if (u && rowne(u->sciezka, silnik_.planowana_sciezka())) (void)okl_.wczytaj(p_.pliki, u->sciezka, u->meta);
            ++w_utw_;
        }
        silnik_.pompuj();
        const uint32_t z = silnik_.zdarzenia();
        if (z & ZD_STAN) ++w_utw_;
        if (z & ZD_KONIEC_UTWORU) po_koncu_utworu();
        if (z & ZD_BLAD) po_bledzie();
        if (z & ZD_ZALADOWANO) { proby_pod_rzad_ = 0; if (rodzaj_ == RodzajKomunikatu::Blad) ustaw_komunikat("", RodzajKomunikatu::Informacja); automatyczne_ = false; }
        aktualizuj_komunikat_niedoboru(z);
        if (bib_.skanuje()) {
            if (bib_.krok(1)) po_skanie();
        }
        if (do_zapisu_ && p_.czas_ns) {
            const uint64_t teraz = p_.czas_ns();
            if (teraz - ostatni_zapis_ >= ODSTEP_ZAPISU_NS) zapisz_konfiguracje(teraz);
        }
        if (g_profiler.wlaczony() && p_.czas_ns && g_profiler.zamknij_okno(p_.czas_ns(), OKNO_DIAGNOSTYKI_NS)) {
            opisz_diagnostyke(diag_, sizeof(diag_));
            ++w_diag_;
        }
    }

    // Komunikat o niedoborze (migawka licznikow w chwili zlozenia):
    //  - najwyzej co ODSTEP_NIEDOBORU_NS (kazda zmiana tekstu to odrysowanie paska; pod emulacja odrysowanie samo moze
    //    wywolac kolejny niedobor - sprzezenie zwrotne zmierzone w QEMU). Niedobor zgloszony w tym oknie NIE ginie:
    //    zostaje oczekujacy i komunikat jest odswiezany po uplywie okna (wczesniej czekal na kolejne zdarzenie),
    //  - na granicy utworu (Silnik::pokolenie_utworu) aktywny komunikat jest skladany od nowa natychmiast: inaczej
    //    tekst "X ms w tym utworze" z poprzedniego utworu byl wyswietlany przy nowym (natywnie: 192 ms WAV z fazy C
    //    widoczne przy MP3, potem 21 ms nowego utworu - pozorny spadek licznika, ktory sam jest monotoniczny).
    void aktualizuj_komunikat_niedoboru(uint32_t z) {
        const uint64_t teraz = p_.czas_ns ? p_.czas_ns() : 0;
        if (z & ZD_NIEDOBOR) niedobor_oczekuje_ = true;
        const bool okno_minelo = !p_.czas_ns || teraz - ostatni_niedobor_ >= ODSTEP_NIEDOBORU_NS || !komunikat_niedoboru_aktywny_;
        const bool inny_utwor = komunikat_niedoboru_aktywny_ && pokolenie_komunikatu_ != silnik_.pokolenie_utworu();
        if (!(niedobor_oczekuje_ && okno_minelo) && !inny_utwor) return;
        if (niedobor_oczekuje_ && okno_minelo) ostatni_niedobor_ = teraz;
        niedobor_oczekuje_ = niedobor_oczekuje_ && !okno_minelo;
        opisz_niedobor(komunikat_niedoboru_, sizeof(komunikat_niedoboru_));
        ustaw_komunikat(komunikat_niedoboru_, RodzajKomunikatu::Ostrzezenie);
        komunikat_niedoboru_aktywny_ = true;
        pokolenie_komunikatu_ = silnik_.pokolenie_utworu();
    }
    bool komunikat_niedoboru_aktywny() const { return komunikat_niedoboru_aktywny_; }
    bool niedobor_oczekuje() const { return niedobor_oczekuje_; }

    // Tresc komunikatu o niedoborze (tez dla testow): utwor i sesja osobno, ze zrodlem liczby.
    void opisz_niedobor(char* bufor, size_t cap) const {
        const StatystykiCiaglosci& c = silnik_.ciaglosc();
        Tekst t(bufor, cap);
        t.dodaj("Przerwy w dźwięku – brak danych Grajka: ");
        t.liczba(StatystykiCiaglosci::ms(c.niedobor_utworu));
        t.dodaj(" ms w tym utworze, ");
        t.liczba(StatystykiCiaglosci::ms(c.niedobor_sesji));
        t.dodaj(" ms od uruchomienia.");
    }
    // Krotkie podsumowanie do paska stanu (pelne liczniki: wiersze_diagnostyki / karta "Diagnostyka").
    void opisz_diagnostyke(char* bufor, size_t cap) const {
        const OknoPomiaru& o = g_profiler.ostatnie();
        const StatystykiCiaglosci& c = silnik_.ciaglosc();
        Tekst t(bufor, cap);
        t.dodaj("Niedobór "); t.liczba(StatystykiCiaglosci::ms(c.niedobor_utworu));
        t.dodaj(" ms · przerwa ");  t.liczba(c.maks_przerwa_producenta_ns / 1000000ULL);
        t.dodaj(" ms · GUI "); t.liczba((o.suma_ns(Sekcja::OknoRysowanie) + o.suma_ns(Sekcja::OknoPublikacja)) / 1000000ULL);
        t.dodaj(" ms/s");
    }

    // Pelna lista licznikow (karta "Diagnostyka"): nazwa i wartosc osobno, bez obcinania. Liczniki ciaglosci sa
    // zawsze dostepne; czasy sekcji - gdy pomiar (opcja Diagnostyka) jest wlaczony. Czasy sekcji to czas zegarowy
    // TSC z ostatniego okna ~1 s: obejmuja wywlaszczenie procesu i oczekiwanie w wywolaniach systemowych.
    static constexpr size_t LICZBA_POMIAROW = 23;
    void pomiar(size_t i, char* nazwa, size_t cap_n, char* wartosc, size_t cap_w) const {
        Tekst n(nazwa, cap_n), w(wartosc, cap_w);
        const StatystykiCiaglosci& c = silnik_.ciaglosc();
        const OknoPomiaru& o = g_profiler.ostatnie();
        const bool prof = g_profiler.wlaczony() && o.ns;
        auto ms1 = [&w](uint64_t ns) { w.liczba(ns / 1000000ULL); w.dodaj(","); w.liczba((ns / 100000ULL) % 10); };
        auto sekcja = [&](const char* nz, Sekcja sk, bool maks) {
            n.dodaj(nz);
            if (!prof) { w.dodaj("—"); return; }
            if (maks) { ms1(o.maks_ns(sk)); w.dodaj(" ms"); } else { ms1(o.suma_ns(sk)); w.dodaj(" ms/s"); }
        };
        switch (i) {
            case 0: n.dodaj("Niedobór – utwór"); w.liczba(StatystykiCiaglosci::ms(c.niedobor_utworu)); w.dodaj(" ms"); break;
            case 1: n.dodaj("Niedobór – sesja"); w.liczba(StatystykiCiaglosci::ms(c.niedobor_sesji)); w.dodaj(" ms"); break;
            case 2: n.dodaj("Epizody niedoboru"); w.liczba(c.epizody_utworu); break;
            case 3: n.dodaj("Maks. przerwa"); w.liczba(c.maks_przerwa_producenta_ns / 1000000ULL); w.dodaj(" ms"); break;
            case 4: n.dodaj("Przerwy > 85 ms"); w.liczba(c.przerwy_ponad_kolejke); break;
            case 5: n.dodaj("Min. kolejka PCM");
                if (c.min_kolejka == 0xFFFFFFFFu) w.dodaj("—");
                else { w.liczba(StatystykiCiaglosci::ms(c.min_kolejka)); w.dodaj(" ms"); }
                break;
            case 6: n.dodaj("Kolejka PCM teraz"); w.liczba(StatystykiCiaglosci::ms(silnik_.w_kolejce())); w.dodaj(" ms"); break;
            case 7: sekcja("Dekodowanie", Sekcja::Dekodowanie, false); break;
            case 8: sekcja("Konwersja", Sekcja::Konwersja, false); break;
            case 9: sekcja("Korektor", Sekcja::Korektor, false); break;
            case 10: sekcja("Analiza", Sekcja::Analiza, false); break;
            case 11: sekcja("BWS 110", Sekcja::Bws110, false); break;
            case 12: sekcja("Widmo (FFT)", Sekcja::Widmo, false); break;
            case 13: sekcja("Rysowanie", Sekcja::OknoRysowanie, false); break;
            case 14: sekcja("Maks. rysowanie", Sekcja::OknoRysowanie, true); break;
            case 15: sekcja("– tabele", Sekcja::Tabele, false); break;
            case 16: sekcja("– wizualizacja", Sekcja::Wizualizacja, false); break;
            case 17: sekcja("Publikacja BWS 57", Sekcja::OknoPublikacja, false); break;
            case 18: sekcja("Maks. publikacja", Sekcja::OknoPublikacja, true); break;
            case 19: n.dodaj("Odrysowania");
                if (prof) { w.liczba(o.odrysowania); w.dodaj(" /s"); } else w.dodaj("—");
                break;
            case 20: sekcja("Okładka", Sekcja::Okladka, false); break;
            // planista jadra (sesja): gotowy, ale CPU zajete przez inny watek / najdluzsze dobrowolne uspienie
            case 21: n.dodaj("Maks. czekanie na CPU"); w.liczba(c.maks_czekanie_cpu_ns / 1000000ULL); w.dodaj(" ms"); break;
            case 22: n.dodaj("Maks. uśpienie procesu"); w.liczba(c.maks_uspienie_ns / 1000000ULL); w.dodaj(" ms"); break;
            default: break;
        }
    }
    void ustaw_diagnostyke(bool w) {
        konf_.diagnostyka = w;
        g_profiler.ustaw_wlaczony(w, p_.czas_ns ? p_.czas_ns() : 0);
        diag_[0] = '\0';
        ++w_diag_;
        zmieniono();
    }

    // ------------------------------------------------------------ rodzaj pamieci (BWS 111)
    // Trwaly: /dane/muzyka na zainstalowanym dysku BSP2. Ram: sesja Live (albo awaryjny RAM FS) - pliki
    // znikna po restarcie. Nieznany: starsze jadro bez BWS 111 (komunikat bez dopisku).
    enum class RodzajMagazynu : uint8_t { Nieznany, Trwaly, Ram };
    RodzajMagazynu rodzaj_magazynu() const { return rodzaj_magazynu_; }
    void sprawdz_magazyn() {
        rodzaj_magazynu_ = RodzajMagazynu::Nieznany;
        if (!p_.bws) return;
        BwsMagazynV1 m{};
        if (p_.bws(111, reinterpret_cast<uint64_t>(&m), sizeof(m), BWS_MAGAZYN_V1, 0) != BWS_MAG_OK) return;
        if (m.wersja != BWS_MAGAZYN_V1 || m.rozmiar != sizeof(m) || !(m.flagi & BWS_MAG_GOTOWY)) return;
        rodzaj_magazynu_ = (m.flagi & BWS_MAG_TRWALY) ? RodzajMagazynu::Trwaly : RodzajMagazynu::Ram;
    }

    // ------------------------------------------------------------ biblioteka
    void odswiez_biblioteke() {
        if (bib_.skanuje()) return;
        bib_.ustaw_podkatalogi(konf_.podkatalogi);
        // zapamietaj sciezki kolejki i biezacego utworu (indeksy zmienia publikacja nowej listy)
        zapamietaj_sciezki();
        if (!bib_.rozpocznij()) { ustaw_komunikat("Brak pamięci do skanowania biblioteki.", RodzajKomunikatu::Blad); return; }
        ustaw_komunikat("Wyszukiwanie muzyki w /dane/muzyka…", RodzajKomunikatu::Informacja);
        ++w_bib_;
    }
    void filtruj(const char* wzorzec) { bib_.filtruj(wzorzec); ++w_bib_; }

    // ------------------------------------------------------------ polecenia odtwarzania
    // Dwuklik w bibliotece: kolejka = biezacy widok (filtr), start od wybranego wiersza.
    bool odtworz_z_widoku(uint32_t wiersz) {
        const int idx = bib_.widoczny(wiersz);
        if (idx < 0) return false;
        uint16_t* lista = new uint16_t[MAKS_KOLEJKA];
        if (!lista) return false;
        uint32_t n = 0;
        for (uint32_t w = 0; w < bib_.widoczne() && n < MAKS_KOLEJKA; ++w) lista[n++] = uint16_t(bib_.widoczny(w));
        kol_.ustaw(lista, n, int(wiersz));
        delete[] lista;
        ++w_kol_;
        return graj_pozycje(int(wiersz), false);
    }
    bool odtworz_z_kolejki(uint32_t poz) {
        if (poz >= kol_.liczba()) return false;
        kol_.ustaw_biezaca(int(poz));
        ++w_kol_;
        return graj_pozycje(int(poz), false);
    }
    // "Odtworz": wznowienie pauzy, start zaladowanego utworu albo pierwszego z kolejki / biblioteki.
    bool odtworz() {
        const Stan s = silnik_.stan();
        if (s == Stan::Pauza) return silnik_.wznow();
        if (s == Stan::Odtwarzanie || s == Stan::Ladowanie) return true;
        if (silnik_.zaladowany() && s == Stan::Zatrzymany) return silnik_.odtwarzaj();
        if (kol_.liczba()) return odtworz_z_kolejki(kol_.biezaca() >= 0 ? uint32_t(kol_.biezaca()) : 0);
        if (bib_.widoczne()) return odtworz_z_widoku(0);
        ustaw_komunikat("Brak utworów do odtworzenia. Umieść pliki WAV lub MP3 w /dane/muzyka.", RodzajKomunikatu::Ostrzezenie);
        return false;
    }
    bool pauza() { return silnik_.pauza(); }
    bool przelacz_pauze() { return silnik_.stan() == Stan::Odtwarzanie ? silnik_.pauza() : odtworz(); }
    bool stop() { const bool r = silnik_.zatrzymaj(); ++w_utw_; return r; }
    bool nastepny() {
        const int n = kol_.nastepna(false);
        if (n < 0) { ustaw_komunikat("To jest ostatni utwór na liście.", RodzajKomunikatu::Informacja); return false; }
        kol_.przejdz(n); ++w_kol_;
        return graj_pozycje(n, false);
    }
    bool poprzedni() {
        if (silnik_.pozycja_ms() > 3000 && silnik_.zaladowany()) return silnik_.przewin(0);
        const int n = kol_.poprzednia();
        if (n < 0) return false;
        kol_.przejdz(n); ++w_kol_;
        return graj_pozycje(n, false);
    }
    bool przewin(uint64_t ms) { const bool r = silnik_.przewin(ms); ++w_utw_; return r; }

    // ------------------------------------------------------------ kolejka
    bool dodaj_do_kolejki(int indeks) {
        if (indeks < 0 || uint32_t(indeks) >= bib_.liczba()) return false;
        if (!kol_.dodaj(uint16_t(indeks))) { ustaw_komunikat("Kolejka jest pełna.", RodzajKomunikatu::Ostrzezenie); return false; }
        ++w_kol_;
        Tekst t(bufor_kom_, sizeof(bufor_kom_)); t.dodaj("Dodano do kolejki: "); t.dodaj(bib_.utwor(uint32_t(indeks))->tytul());
        ustaw_komunikat(bufor_kom_, RodzajKomunikatu::Sukces);
        return true;
    }
    bool usun_z_kolejki(uint32_t poz) { const bool r = kol_.usun(poz); if (r) ++w_kol_; return r; }
    bool przesun_w_kolejce(uint32_t poz, int kierunek) { const bool r = kol_.przesun(poz, kierunek); if (r) ++w_kol_; return r; }
    void wyczysc_kolejke() { kol_.wyczysc(); ++w_kol_; }

    bool zapisz_liste() {
        if (!p_.zapisz_ustawienie) return false;
        char* t = new char[MAKS_M3U8];
        if (!t) return false;
        const uint32_t ile = kol_.zapisz_m3u8(bib_, t, 4000);    // ustawienia_zapisz: wartosc < 4095 B z NUL
        const bool ok = p_.zapisz_ustawienie(KLUCZ_LISTY, t);
        delete[] t;
        Tekst k(bufor_kom_, sizeof(bufor_kom_));
        if (!ok) { ustaw_komunikat("Nie udało się zapisać listy odtwarzania.", RodzajKomunikatu::Blad); return false; }
        k.dodaj("Zapisano listę odtwarzania (M3U8): "); k.liczba(ile); k.dodaj(" z "); k.liczba(kol_.liczba()); k.dodaj(" utworów.");
        ustaw_komunikat(bufor_kom_, ile == kol_.liczba() ? RodzajKomunikatu::Sukces : RodzajKomunikatu::Ostrzezenie);
        return true;
    }
    bool wczytaj_liste() {
        if (!p_.wczytaj_ustawienie) return false;
        char* t = new char[MAKS_M3U8 + 1];
        if (!t) return false;
        zeruj(t, MAKS_M3U8 + 1);
        if (!p_.wczytaj_ustawienie(KLUCZ_LISTY, t, MAKS_M3U8)) {
            delete[] t;
            ustaw_komunikat("Brak zapisanej listy odtwarzania.", RodzajKomunikatu::Informacja);
            return false;
        }
        const auto w = kol_.wczytaj_m3u8(bib_, t, dlugosc(t, MAKS_M3U8));
        delete[] t;
        ++w_kol_;
        Tekst k(bufor_kom_, sizeof(bufor_kom_));
        k.dodaj("Wczytano listę: "); k.liczba(w.dodane); k.dodaj(" utworów");
        if (w.pominiete) { k.dodaj(", brak w bibliotece: "); k.liczba(w.pominiete); }
        if (w.odrzucone) { k.dodaj(", odrzucone ścieżki: "); k.liczba(w.odrzucone); }
        k.dodaj(".");
        ustaw_komunikat(bufor_kom_, w.odrzucone ? RodzajKomunikatu::Ostrzezenie : RodzajKomunikatu::Sukces);
        return true;
    }

    // ------------------------------------------------------------ ustawienia
    void ustaw_glosnosc(uint32_t v) { konf_.glosnosc = v > 100 ? 100 : v; silnik_.ustaw_glosnosc(konf_.glosnosc, konf_.wyciszony); zmieniono(); }
    void przelacz_wyciszenie() { konf_.wyciszony = !konf_.wyciszony; silnik_.ustaw_glosnosc(konf_.glosnosc, konf_.wyciszony); zmieniono(); }
    void ustaw_losowo(bool w) { konf_.losowo = w; kol_.ustaw_losowo(w, p_.czas_ns ? p_.czas_ns() | 1 : 0x1234567ULL); ++w_kol_; zmieniono(); }
    void nastepne_powtarzanie() {
        konf_.powtarzanie = Powtarzanie((uint8_t(konf_.powtarzanie) + 1) % 3);
        kol_.ustaw_powtarzanie(konf_.powtarzanie);
        zmieniono();
    }
    void ustaw_w_tle(bool w) { konf_.w_tle = w; zmieniono(); }
    void ustaw_wizualizacje(bool w) { konf_.wizualizacja = w; silnik_.analizator().ustaw_wlaczony(w); zmieniono(); }
    void ustaw_podkatalogi(bool w) { konf_.podkatalogi = w; zmieniono(); }
    void ustaw_korektor(bool w) { konf_.korektor = w; silnik_.korektor().ustaw_wlaczony(w); zmieniono(); }
    void ustaw_pasmo(int pasmo, int db) {
        silnik_.korektor().ustaw_wzmocnienie(pasmo, db);
        if (pasmo >= 0 && pasmo < Tabele::LICZBA_PASM) konf_.pasma[pasmo] = int8_t(silnik_.korektor().wzmocnienie(pasmo));
        zmieniono();
    }
    void ustaw_preset(PresetKorektora p) {
        if (p == PresetKorektora::Wlasny) return;
        silnik_.korektor().ustaw_preset(p);
        for (int i = 0; i < Tabele::LICZBA_PASM; ++i) konf_.pasma[i] = int8_t(silnik_.korektor().wzmocnienie(i));
        zmieniono();
    }

    // Okno zminimalizowane / przywrocone (wizualizacja nie jest liczona w tle; bez odtwarzania w tle - pauza).
    void okno_zminimalizowane(bool zminimalizowane) {
        if (zminimalizowane == zminimalizowane_) return;
        zminimalizowane_ = zminimalizowane;
        silnik_.analizator().ustaw_wlaczony(!zminimalizowane && konf_.wizualizacja);
        if (zminimalizowane && !konf_.w_tle && silnik_.stan() == Stan::Odtwarzanie) {
            silnik_.pauza();
            ustaw_komunikat("Wstrzymano: okno zminimalizowane, a odtwarzanie w tle jest wyłączone.", RodzajKomunikatu::Informacja);
        }
    }
    // Zamkniecie okna: true = proces ma zostac (odtwarzanie w tle), false = zakonczyc (strumien zamkniety).
    bool przy_zamknieciu() {
        const Stan s = silnik_.stan();
        if (konf_.w_tle && (s == Stan::Odtwarzanie || s == Stan::Pauza || s == Stan::Ladowanie)) {
            ustaw_komunikat("Grajek gra w tle. Przywróć okno z paska zadań; przycisk „Zakończ” zamyka odtwarzacz.", RodzajKomunikatu::Informacja);
            return true;
        }
        zakoncz();
        return false;
    }
    // Pelne zakonczenie: zapis ustawien, zamkniecie strumienia PCM, zwolnienie dekodera.
    void zakoncz() {
        if (do_zapisu_) zapisz_konfiguracje(p_.czas_ns ? p_.czas_ns() : 0);
        // Diagnostyka do odczytu po sesji (np. po tescie natywnym): ostatnie okno i liczniki ciaglosci.
        if (konf_.diagnostyka && p_.zapisz_ustawienie && diag_[0]) {
            char t[1536];
            Tekst x(t, sizeof(t));
            for (size_t i = 0; i < LICZBA_POMIAROW; ++i) {
                char n[48], w[32]; pomiar(i, n, sizeof(n), w, sizeof(w));
                x.dodaj(n); x.dodaj(": "); x.dodaj(w); x.nowa_linia();
            }
            (void)p_.zapisz_ustawienie("diagnostyka", t);
        }
        silnik_.zwolnij();
    }

private:
    void zastosuj_konfiguracje() {
        silnik_.ustaw_glosnosc(konf_.glosnosc, konf_.wyciszony);
        kol_.ustaw_powtarzanie(konf_.powtarzanie);
        kol_.ustaw_losowo(konf_.losowo, p_.czas_ns ? p_.czas_ns() | 1 : 0x1234567ULL);
        Korektor& k = silnik_.korektor();
        for (int i = 0; i < Tabele::LICZBA_PASM; ++i) k.ustaw_wzmocnienie(i, konf_.pasma[i]);
        k.ustaw_wlaczony(konf_.korektor);
        silnik_.analizator().ustaw_wlaczony(konf_.wizualizacja);
        bib_.ustaw_podkatalogi(konf_.podkatalogi);
        g_profiler.ustaw_wlaczony(konf_.diagnostyka, p_.czas_ns ? p_.czas_ns() : 0);
        ++w_ust_;
    }
    void zmieniono() {
        do_zapisu_ = true;
        ++w_ust_;
    }
    void zapisz_konfiguracje(uint64_t teraz) {
        do_zapisu_ = false;
        ostatni_zapis_ = teraz;
        if (!p_.zapisz_ustawienie) return;
        char t[1024];
        konf_.zapisz(t, sizeof(t));
        if (!p_.zapisz_ustawienie(KLUCZ_KONFIGURACJI, t))
            ustaw_komunikat("Nie udało się zapisać ustawień Grajka.", RodzajKomunikatu::Ostrzezenie);
    }
    void ustaw_komunikat(const char* t, RodzajKomunikatu r) {
        if (t != komunikat_) kopiuj_tekst(komunikat_, sizeof(komunikat_), t);
        rodzaj_ = r;
        komunikat_niedoboru_aktywny_ = false;     // inny komunikat zastapil migawke niedoboru
        ++w_kom_;
    }
    bool graj_pozycje(int poz, bool automatycznie) {
        const int idx = kol_.utwor(uint32_t(poz));
        const Utwor* u = idx >= 0 ? bib_.utwor(uint32_t(idx)) : nullptr;
        if (!u) return false;
        biezacy_ = idx;
        automatyczne_ = automatycznie;
        okl_.wyczysc();
        okladka_do_wczytania_ = u->meta.okladka != TypOkladki::Brak;
        ++w_utw_; ++w_kol_;
        return silnik_.zaplanuj(u->sciezka, u->format, true);
    }
    void po_koncu_utworu() {
        const int n = kol_.nastepna(true);
        if (n < 0) {
            ustaw_komunikat("Koniec listy odtwarzania.", RodzajKomunikatu::Informacja);
            ++w_utw_;
            return;
        }
        kol_.przejdz(n);
        (void)graj_pozycje(n, true);
    }
    void po_bledzie() {
        Tekst t(bufor_kom_, sizeof(bufor_kom_));
        const Utwor* u = utwor_biezacy();
        if (u) { t.dodaj("„"); t.dodaj(u->tytul()); t.dodaj("”: "); }
        t.dodaj(silnik_.blad());
        ustaw_komunikat(bufor_kom_, RodzajKomunikatu::Blad);
        ++w_utw_;
        // Przejscie automatyczne na uszkodzony plik: sprobuj nastepnego, ale nie w nieskonczonosc.
        if (automatyczne_ && ++proby_pod_rzad_ < kol_.liczba()) {
            const int n = kol_.nastepna(false);
            if (n >= 0) { kol_.przejdz(n); (void)graj_pozycje(n, true); }
        } else {
            proby_pod_rzad_ = 0;
        }
    }
    void zapamietaj_sciezki() {
        delete[] stare_;
        stare_n_ = kol_.liczba();
        stare_ = stare_n_ ? new char[stare_n_][MAKS_SCIEZKA] : nullptr;
        for (uint32_t i = 0; i < stare_n_ && stare_; ++i) {
            const Utwor* u = bib_.utwor(uint32_t(kol_.utwor(i)));
            kopiuj_tekst(stare_[i], MAKS_SCIEZKA, u ? u->sciezka : "");
        }
        const Utwor* b = utwor_biezacy();
        kopiuj_tekst(stara_biezaca_, sizeof(stara_biezaca_), b ? b->sciezka : "");
    }
    void dopisz_magazyn(Tekst& t) const {
        if (rodzaj_magazynu_ == RodzajMagazynu::Trwaly) t.dodaj(" Pliki są na dysku trwałym.");
        else if (rodzaj_magazynu_ == RodzajMagazynu::Ram)
            t.dodaj(" Sesja Live: system plików działa w RAM – pliki znikną po restarcie.");
    }
    void po_skanie() {
        sprawdz_magazyn();
        if (stare_) { (void)kol_.przemapuj(stare_, bib_); delete[] stare_; stare_ = nullptr; stare_n_ = 0; }
        biezacy_ = stara_biezaca_[0] ? bib_.znajdz(stara_biezaca_) : -1;
        ++w_bib_; ++w_kol_; ++w_utw_;
        const WynikSkanu& w = bib_.wynik();
        Tekst t(bufor_kom_, sizeof(bufor_kom_));
        if (!w.katalog_istnieje) {
            t.dodaj("Katalog /dane/muzyka nie istnieje. Utwórz go i umieść w nim pliki WAV lub MP3, potem kliknij „Odśwież bibliotekę”.");
            dopisz_magazyn(t);
            ustaw_komunikat(bufor_kom_, RodzajKomunikatu::Ostrzezenie);
            return;
        }
        if (!bib_.liczba()) {
            t.dodaj("Brak obsługiwanych plików w /dane/muzyka (WAV, MP3).");
            if (w.odrzucone) { t.dodaj(" Odrzucono: "); t.liczba(w.odrzucone); t.dodaj(" – "); t.dodaj(w.ostatni_blad); }
            dopisz_magazyn(t);
            ustaw_komunikat(bufor_kom_, RodzajKomunikatu::Informacja);
            return;
        }
        t.dodaj("Biblioteka: "); t.liczba(bib_.liczba()); t.dodaj(bib_.liczba() == 1 ? " utwór" : " utworów");
        if (w.odrzucone) { t.dodaj(", pominięte (nieprawidłowe): "); t.liczba(w.odrzucone); }
        if (w.pominiete_limit) { t.dodaj(", poza limitem: "); t.liczba(w.pominiete_limit); }
        t.dodaj(".");
        dopisz_magazyn(t);
        ustaw_komunikat(bufor_kom_, w.odrzucone || w.pominiete_limit || rodzaj_magazynu_ == RodzajMagazynu::Ram
                                        ? RodzajKomunikatu::Ostrzezenie : RodzajKomunikatu::Sukces);
    }

    Platforma p_{};
    Silnik silnik_;
    Biblioteka bib_;
    Kolejka kol_;
    Konfiguracja konf_;
    Okladka okl_;
    int biezacy_ = -1;
    bool okladka_do_wczytania_ = false, automatyczne_ = false, zminimalizowane_ = false, do_zapisu_ = false;
    uint32_t proby_pod_rzad_ = 0;
    uint64_t ostatni_zapis_ = 0, ostatni_niedobor_ = 0;
    bool niedobor_oczekuje_ = false, komunikat_niedoboru_aktywny_ = false;
    uint32_t pokolenie_komunikatu_ = 0;
    char (*stare_)[MAKS_SCIEZKA] = nullptr;
    uint32_t stare_n_ = 0;
    char stara_biezaca_[MAKS_SCIEZKA]{};
    char komunikat_[192]{};
    char komunikat_niedoboru_[192]{};
    char diag_[128]{};
    char bufor_kom_[320]{};
    RodzajMagazynu rodzaj_magazynu_ = RodzajMagazynu::Nieznany;
    RodzajKomunikatu rodzaj_ = RodzajKomunikatu::Informacja;
    uint32_t w_bib_ = 0, w_kol_ = 0, w_utw_ = 0, w_kom_ = 0, w_ust_ = 0, w_diag_ = 0;

public:
    ~Odtwarzacz() { delete[] stare_; }
};

} // namespace Grajek
