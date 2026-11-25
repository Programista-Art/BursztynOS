#pragma once
/*
 * Grajek - silnik odtwarzania: dekoder -> konwersja 48 kHz stereo -> korektor -> BWS 110 (mikser systemowy).
 *
 * Model wykonania: Ring 3 nie ma watkow uzytkownika, wiec silnik jest kooperacyjnym zadaniem petli BursztynUI
 * (zadanie okresowe co 1 tick = ~4 ms, uruchamiane takze przed kazdym odrysowaniem). pompuj() uzupelnia kolejke
 * strumienia BWS 110 (4096 ramek = 85 ms) do pelna, gdy wolne jest >= 512 ramek, z budzetem zapisow na wywolanie.
 * Bez aktywnego czekania: proces spi w gui_czekaj_na_zdarzenie_z_timeoutem miedzy wywolaniami.
 *
 * Cykl zycia strumienia BWS 110 (jadro/bws/pcm_api.h):
 *   OPEN (48000 Hz, 2 kanaly, S16) - przy starcie odtwarzania,
 *   WRITE (<= 512 ramek) - zaakceptowany prefiks; reszta czeka w buforze posrednim (bez duplikatow i pominiec),
 *   QUERY - zajetosc kolejki (pozycja odtwarzania = zaakceptowane - w kolejce),
 *   QUERY_STATS - licznik "starved" (prawdziwe niedobory), raportowany osobno od pauzy i ciszy,
 *   GAIN - glosnosc 0..100 i wyciszenie strumienia,
 *   FINISH - pauza i koniec utworu: kolejka gra do konca, mikser nie liczy dalszego braku danych jako niedoboru,
 *   CLOSE - stop, przewijanie, zmiana utworu: jadro porzuca kolejke, wiec stare probki nie trafia do nowego utworu.
 * Interfejs nie ma operacji "flush": zamkniecie i ponowne otwarcie jest jedynym poprawnym oproznieniem kolejki.
 *
 * Maszyna stanow: Zatrzymany, Ladowanie, Odtwarzanie, Pauza, Przewijanie, Blad. Niedozwolone przejscia sa
 * odrzucane (metody zwracaja false i nie zmieniaja stanu).
 *
 * Diagnostyka ciaglosci (StatystykiCiaglosci) - znaczenie licznikow:
 *  - niedobor_*: licznik jadra "starved" strumienia Grajka = ramki 48 kHz okresow HDA juz zatwierdzonych do DMA,
 *    w ktorych strumien byl OCZEKIWANY (po prerollu, przed FINISH/CLOSE), a jego kolejka byla pusta. To cisza na
 *    wyjsciu wynikajaca z braku danych Grajka. Nie obejmuje: pauzy i dogrywania po FINISH, ladowania i przelaczania
 *    utworu, prerollu nowego strumienia, okresow pominietych przez transport HDA (utrata wlasnosci DMA) ani ciszy
 *    nagranej w samym pliku. Statystyki sa odczytywane okresowo ORAZ tuz przed kazdym CLOSE/FINISH, wiec koncowka
 *    strumienia nie ginie, a zamkniety strumien nie jest liczony podwojnie (jadro liczy od OPEN, delta od ostatniego
 *    odczytu tego samego uchwytu),
 *  - niedobor_utworu jest monotoniczny w obrebie jednego utworu i zerowany WYLACZNIE na granicy utworu:
 *    wczytaj() (wybor utworu, nastepny/poprzedni, powtorzenie utworu, przejscie automatyczne). Przewijanie,
 *    pauza/wznowienie i ponowne otwarcie strumienia go nie zeruja. Kazda granica zwieksza pokolenie_utworu(), po
 *    ktorym interfejs rozpoznaje, ze komunikat dotyczy poprzedniego utworu; niedobor_sesji - od uruchomienia,
 *  - przerwa producenta: najdluzszy odstep miedzy kolejnymi uzupelnieniami kolejki w stanie Odtwarzanie (zegar
 *    platformy). Odstep dluzszy niz pojemnosc kolejki (4096 ramek = 85,3 ms) musi skonczyc sie niedoborem.
 */
#include "dekoder.h"
#include "przetwarzanie.h"
#include "korektor.h"
#include "analiza.h"
#include "pomiar.h"
#include "jadro/bws/pcm_api.h"

namespace Grajek {

using FunkcjaBws = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

enum class Stan : uint8_t { Zatrzymany, Ladowanie, Odtwarzanie, Pauza, Przewijanie, Blad };

inline const char* nazwa_stanu(Stan s) {
    switch (s) {
        case Stan::Zatrzymany: return "Zatrzymano";
        case Stan::Ladowanie: return "Ładowanie…";
        case Stan::Odtwarzanie: return "Odtwarzanie";
        case Stan::Pauza: return "Pauza";
        case Stan::Przewijanie: return "Przewijanie…";
        case Stan::Blad: return "Błąd";
    }
    return "?";
}

enum Zdarzenie : uint32_t {
    ZD_STAN = 1u << 0,
    ZD_KONIEC_UTWORU = 1u << 1,
    ZD_BLAD = 1u << 2,
    ZD_ZALADOWANO = 1u << 3,
    ZD_NIEDOBOR = 1u << 4,
};

class StrumienPcm {
public:
    void ustaw_bws(FunkcjaBws f) { bws_ = f; }
    bool otwarty() const { return uchwyt_ != 0; }
    uint64_t uchwyt() const { return uchwyt_; }
    // >0: uchwyt; <=0: kod BwsPcmError.
    int64_t otworz() {
        if (uchwyt_) return int64_t(uchwyt_);
        BwsPcmFormat f{BWS_PCM_V1, sizeof(BwsPcmFormat), CZESTOTLIWOSC_WYJSCIA, 2, 1, 0};
        const int64_t r = wywolaj(PCM_OPEN, reinterpret_cast<uint64_t>(&f), sizeof(f), BWS_PCM_V1);
        if (r > 0) uchwyt_ = uint64_t(r);
        return r;
    }
    int64_t zapisz(const int16_t* pcm, uint32_t ramki) {
        return uchwyt_ ? wywolaj(PCM_WRITE, uchwyt_, reinterpret_cast<uint64_t>(pcm), ramki) : int64_t(PCM_HANDLE);
    }
    int64_t stan(BwsPcmState& s) {
        return uchwyt_ ? wywolaj(PCM_QUERY, uchwyt_, reinterpret_cast<uint64_t>(&s), sizeof(s)) : int64_t(PCM_HANDLE);
    }
    int64_t statystyki(BwsPcmStats& s) {
        return uchwyt_ ? wywolaj(PCM_QUERY_STATS, uchwyt_, reinterpret_cast<uint64_t>(&s), sizeof(s)) : int64_t(PCM_HANDLE);
    }
    int64_t glosnosc(uint32_t v, bool wycisz) {
        return uchwyt_ ? wywolaj(PCM_GAIN, uchwyt_, v, wycisz ? 1 : 0) : int64_t(PCM_HANDLE);
    }
    int64_t zakoncz() { return uchwyt_ ? wywolaj(PCM_FINISH, uchwyt_, 0, 0) : int64_t(PCM_HANDLE); }
    // Zamkniecie: przy BUSY ponawiane (ograniczenie prob); uchwyt jest zawsze porzucany lokalnie, a jadro i tak
    // sprzata strumienie procesu przy jego zakonczeniu (pcm_posprzataj_proces).
    int64_t zamknij() {
        if (!uchwyt_) return PCM_OK;
        int64_t r = PCM_BUSY;
        for (int i = 0; i < 8 && r == PCM_BUSY; ++i) r = wywolaj(PCM_CLOSE, uchwyt_, 0, 0);
        uchwyt_ = 0;
        return r;
    }

private:
    int64_t wywolaj(uint64_t op, uint64_t a, uint64_t b, uint64_t c) {
        if (!bws_) return PCM_DEVICE;
        return int64_t(bws_(BWS_PCM, op, a, b, c));
    }
    FunkcjaBws bws_ = nullptr;
    uint64_t uchwyt_ = 0;
};

struct StatystykiCiaglosci {
    uint64_t niedobor_utworu = 0;              // ramki 48 kHz (starved), biezacy utwor
    uint64_t niedobor_sesji = 0;               // ramki 48 kHz (starved), od uruchomienia
    uint32_t epizody_utworu = 0;               // odczyty statystyk z przyrostem "starved" (utwor)
    uint64_t maks_przerwa_producenta_ns = 0;   // utwor
    uint32_t przerwy_ponad_kolejke = 0;        // odstepy producenta > pojemnosc kolejki (utwor)
    uint32_t min_kolejka = 0xFFFFFFFFu;        // najmniejsza zajetosc kolejki widziana przez jadro (utwor)
    // Planista jadra (BwsPcmStats V2, od uruchomienia procesu; sesja): najdluzszy czas, gdy Grajek byl GOTOWY, ale
    // CPU mial inny watek (PID 0: kompozytor/HDA, inne aplikacje), i najdluzsze dobrowolne uspienie. Przerwa
    // producenta >> obu = Grajek sam byl zajety (praca Ring 3 albo dlugie wywolanie systemowe, np. BWS 57).
    uint64_t maks_czekanie_cpu_ns = 0;
    uint64_t maks_uspienie_ns = 0;
    static uint64_t ms(uint64_t ramki) { return ramki * 1000ULL / CZESTOTLIWOSC_WYJSCIA; }
};

class Silnik {
public:
    static constexpr uint32_t PORCJA = BWS_PCM_MAX_FRAMES;    // 512 ramek na zapis
    static constexpr uint32_t BUDZET_ZAPISOW = 16;
    static constexpr uint32_t RAMKI_ZRODLA = 2048;
    static constexpr uint32_t MAKS_PROB_OTWARCIA = 250;       // ~1 s przy 4 ms na tick
    static constexpr uint32_t PROG_DOPELNIENIA = 128;
    static constexpr uint64_t CZAS_KOLEJKI_NS = 4096ULL * 1000000000ULL / CZESTOTLIWOSC_WYJSCIA;   // 85,3 ms

    Silnik() = default;
    ~Silnik() { zwolnij(); }
    Silnik(const Silnik&) = delete;
    Silnik& operator=(const Silnik&) = delete;

    void konfiguruj(const DostepDoPlikow& dostep, FunkcjaBws bws) { dostep_ = dostep; pcm_.ustaw_bws(bws); }
    // Zegar monotoniczny (ns) do pomiaru przerw producenta; bez zegara pomiar przerw jest wylaczony.
    void ustaw_zegar(uint64_t (*zegar)()) { zegar_ = zegar; }

    Stan stan() const { return stan_; }
    const char* blad() const { return blad_; }
    bool zaladowany() const { return dek_ != nullptr; }
    const ParametryStrumienia* parametry() const { return dek_ ? &dek_->parametry() : nullptr; }
    FormatAudio format() const { return dek_ ? dek_->format() : FormatAudio::Nieznany; }
    Korektor& korektor() { return kor_; }
    Analizator& analizator() { return an_; }
    bool strumien_otwarty() const { return pcm_.otwarty(); }

    // ---------------------------------------------------------------- ladowanie
    // Planuje wczytanie (wykonywane w nastepnym pompuj(), aby interfejs zdazyl pokazac "Ladowanie").
    bool zaplanuj(const char* sciezka, FormatAudio f, bool odtwarzaj_po) {
        if (!sciezka || !sciezka[0] || f == FormatAudio::Nieznany) return false;
        if (dlugosc(sciezka, MAKS_SCIEZKA) >= MAKS_SCIEZKA) return false;
        zwolnij();
        kopiuj_tekst(plan_sciezka_, sizeof(plan_sciezka_), sciezka);
        plan_format_ = f;
        plan_odtwarzaj_ = odtwarzaj_po;
        blad_[0] = '\0';
        zmien_stan(Stan::Ladowanie);
        return true;
    }
    // Wczytanie synchroniczne (uzywane przez pompuj() i testy).
    bool wczytaj(const char* sciezka, FormatAudio f) {
        zwolnij();
        nowy_utwor_statystyk();
        blad_[0] = '\0';
        zmien_stan(Stan::Ladowanie);
        dek_ = utworz_dekoder(f);
        if (!dek_) return porazka("Nieobsługiwany format pliku.");
        if (!dek_->otworz(dostep_, sciezka)) {
            char b[MAKS_BLAD]; kopiuj_tekst(b, sizeof(b), dek_->blad());
            delete dek_; dek_ = nullptr;
            return porazka(b[0] ? b : "Nie można otworzyć pliku.");
        }
        const ParametryStrumienia& p = dek_->parametry();
        if (!rs_.ustaw(p.czestotliwosc, p.kanaly)) { delete dek_; dek_ = nullptr; return porazka("Nieobsługiwane parametry dźwięku."); }
        wyczysc_potok();
        baza_wyj_ = 0;
        zmien_stan(Stan::Zatrzymany);
        zdarzenia_ |= ZD_ZALADOWANO;
        return true;
    }
    const char* planowana_sciezka() const { return plan_sciezka_; }

    // ---------------------------------------------------------------- sterowanie
    bool odtwarzaj() {
        if (!dek_) return false;
        if (stan_ == Stan::Odtwarzanie) return true;
        if (stan_ == Stan::Pauza) return wznow();
        if (stan_ != Stan::Zatrzymany) return false;
        if (od_poczatku_) {
            if (!przewin_wewn(0)) return false;
            od_poczatku_ = false;
        }
        proby_otwarcia_ = 0;
        zmien_stan(Stan::Odtwarzanie);
        return true;
    }
    bool pauza() {
        if (stan_ != Stan::Odtwarzanie) return false;
        // Kolejka (<= 85 ms) dogrywa sie; mikser przestaje oczekiwac danych - pauza nie jest niedoborem.
        // Statystyki sa odczytywane PRZED FINISH: niedobory sprzed pauzy naleza do odtwarzania.
        if (pcm_.otwarty()) { sprawdz_niedobory(); PomiarSekcji p(Sekcja::Bws110); (void)pcm_.zakoncz(); }
        ostatnie_uzupelnienie_ns_ = 0;
        zmien_stan(Stan::Pauza);
        return true;
    }
    bool wznow() {
        if (stan_ != Stan::Pauza || !dek_) return false;
        ostatnie_uzupelnienie_ns_ = 0;
        zmien_stan(Stan::Odtwarzanie);
        return true;
    }
    bool zatrzymaj() {
        if (stan_ == Stan::Ladowanie) { plan_sciezka_[0] = '\0'; zmien_stan(dek_ ? Stan::Zatrzymany : Stan::Zatrzymany); return true; }
        if (!dek_ && stan_ != Stan::Blad) return stan_ == Stan::Zatrzymany;
        zamknij_strumien();
        wyczysc_potok();
        baza_wyj_ = 0; zapisane_ = 0; w_kolejce_ = 0;
        od_poczatku_ = true;
        zmien_stan(Stan::Zatrzymany);
        return true;
    }
    // Przewiniecie do ms (z zachowaniem odtwarzania/pauzy). Dokladnosc: ramka zrodla (WAV dokladnie, MP3 przez
    // tablice punktow dr_mp3 i dekodowanie do wskazanej ramki).
    bool przewin(uint64_t ms) {
        if (!dek_) return false;
        if (stan_ != Stan::Odtwarzanie && stan_ != Stan::Pauza && stan_ != Stan::Zatrzymany) return false;
        const Stan poprzedni = stan_;
        zmien_stan(Stan::Przewijanie);
        const uint32_t sr = dek_->parametry().czestotliwosc;
        uint64_t cel = ms * sr / 1000ULL;
        const uint64_t dl = dek_->parametry().ramki;
        if (dl && cel >= dl) cel = dl ? dl - 1 : 0;
        if (!przewin_wewn(cel)) return false;
        od_poczatku_ = false;
        zmien_stan(poprzedni);
        return true;
    }
    void zwolnij() {
        zamknij_strumien();
        delete dek_;
        dek_ = nullptr;
        wyczysc_potok();
        baza_wyj_ = 0; zapisane_ = 0; w_kolejce_ = 0;
        od_poczatku_ = false;
        plan_sciezka_[0] = '\0';
        if (stan_ != Stan::Zatrzymany) zmien_stan(Stan::Zatrzymany);
    }

    // ---------------------------------------------------------------- glosnosc (BWS 110 PCM_GAIN)
    void ustaw_glosnosc(uint32_t v, bool wycisz) {
        if (v > 100) v = 100;
        glosnosc_ = v; wyciszony_ = wycisz;
        zastosuj_glosnosc_ = true;
        if (pcm_.otwarty()) zastosuj_glosnosc();
    }
    uint32_t glosnosc() const { return glosnosc_; }
    bool wyciszony() const { return wyciszony_; }

    // ---------------------------------------------------------------- pozycja i statystyki
    uint64_t pozycja_ms() const {
        const uint64_t odtworzone = zapisane_ > w_kolejce_ ? zapisane_ - w_kolejce_ : 0;
        return (baza_wyj_ + odtworzone) * 1000ULL / CZESTOTLIWOSC_WYJSCIA;
    }
    uint64_t czas_ms() const {
        if (!dek_ || !dek_->parametry().czestotliwosc) return 0;
        return dek_->parametry().ramki * 1000ULL / dek_->parametry().czestotliwosc;
    }
    uint32_t w_kolejce() const { return w_kolejce_; }
    // Niedobor strumienia Grajka od uruchomienia (ramki 48 kHz); szczegoly: ciaglosc().
    uint64_t niedobory_ramek() const { return ciag_.niedobor_sesji; }
    const StatystykiCiaglosci& ciaglosc() const { return ciag_; }
    // Numer granicy utworu (zwiekszany przy kazdym wczytaj()): rozpoznanie, do ktorego utworu odnosi sie komunikat.
    uint32_t pokolenie_utworu() const { return pokolenie_utworu_; }
    uint64_t zajete_odpowiedzi() const { return zajete_; }
    uint64_t czesciowe_zapisy() const { return czesciowe_; }
    uint64_t wszystkie_zapisane() const { return calkowicie_zapisane_; }

    // Pobiera i zeruje zdarzenia od ostatniego wywolania.
    uint32_t zdarzenia() { const uint32_t z = zdarzenia_; zdarzenia_ = 0; return z; }

    // ---------------------------------------------------------------- praca okresowa
    void pompuj() {
        if (stan_ == Stan::Ladowanie) {
            if (!plan_sciezka_[0]) return;
            char sc[MAKS_SCIEZKA]; kopiuj_tekst(sc, sizeof(sc), plan_sciezka_);
            const bool odtw = plan_odtwarzaj_;
            const FormatAudio f = plan_format_;
            plan_sciezka_[0] = '\0';
            if (wczytaj(sc, f) && odtw) (void)odtwarzaj();
            return;
        }
        if (stan_ == Stan::Pauza) {
            aktualizuj_kolejke();
            // Po dograniu kolejki strumien jest zamykany: wznowienie otwiera nowy (z prerollem jadra), wiec pierwsze
            // porcje po pauzie nie sa liczone jako niedobor, a bezczynny proces nie trzyma strumienia ani DMA.
            // Niezapisane ramki bufora posredniego zostaja - nic nie ginie i nic nie jest powielane.
            if (pcm_.otwarty() && w_kolejce_ == 0) {
                baza_wyj_ += zapisane_; zapisane_ = 0;
                zamknij_strumien();
            }
            return;
        }
        if (stan_ != Stan::Odtwarzanie) return;
        if (!pcm_.otwarty()) {
            const int64_t r = pcm_.otworz();
            if (r <= 0) {
                if (r == PCM_DEVICE) { porazka("Brak urządzenia dźwięku (Intel HDA)."); return; }
                if (r == PCM_UNSUPPORTED || r == PCM_INVALID) { porazka("System odrzucił format strumienia PCM."); return; }
                if (++proby_otwarcia_ > MAKS_PROB_OTWARCIA) {
                    porazka(r == PCM_LIMIT ? "Za dużo otwartych strumieni dźwięku w systemie." : "Urządzenie dźwięku jest zajęte.");
                }
                return;   // BUSY/LIMIT: ponowienie w nastepnym ticku
            }
            proby_otwarcia_ = 0;
            zapisane_ = 0; w_kolejce_ = 0; ostatnie_starved_ = 0;
            zastosuj_glosnosc_ = true;
        }
        if (zastosuj_glosnosc_) zastosuj_glosnosc();
        zmierz_przerwe_producenta();
        uzupelnij(PORCJA);
        if (++licznik_statystyk_ >= 50) { licznik_statystyk_ = 0; sprawdz_niedobory(); }
    }

    // Dopelnienie kolejki z punktow kontrolnych interfejsu (tuz przed kosztownym rysowaniem i publikacja okna).
    // Bezpieczne w trakcie rysowania: nie laduje plikow, nie otwiera strumienia, nie zmienia stanu odtwarzania
    // (poza bledem strumienia, ktory i tak konczy odtwarzanie). Pisze juz od 128 wolnych ramek.
    void dopompuj() {
        if (stan_ != Stan::Odtwarzanie || !pcm_.otwarty() || !dek_ || finish_wyslany_) return;
        zmierz_przerwe_producenta();
        uzupelnij(PROG_DOPELNIENIA);
    }

private:
    bool porazka(const char* tekst) {
        kopiuj_tekst(blad_, sizeof(blad_), tekst);
        zamknij_strumien();
        zmien_stan(Stan::Blad);
        zdarzenia_ |= ZD_BLAD;
        return false;
    }
    void zmien_stan(Stan s) { if (s != stan_) { stan_ = s; zdarzenia_ |= ZD_STAN; } }
    // Kazde zamkniecie strumienia: najpierw ostatni odczyt "starved" tego uchwytu (inaczej koncowka ginie).
    void zamknij_strumien() {
        // Przestoj producenta trwajacy do chwili zamkniecia (np. przewiniecie po dlugim rysowaniu) tez jest liczony.
        if (pcm_.otwarty() && ostatnie_uzupelnienie_ns_) zmierz_przerwe_producenta();
        if (pcm_.otwarty()) sprawdz_niedobory();
        pcm_.zamknij();
        ostatnie_starved_ = 0;
        ostatnie_uzupelnienie_ns_ = 0;
    }
    void nowy_utwor_statystyk() {
        const uint64_t sesja = ciag_.niedobor_sesji;
        const uint64_t czekanie = ciag_.maks_czekanie_cpu_ns, uspienie = ciag_.maks_uspienie_ns;
        ciag_ = StatystykiCiaglosci{};
        ciag_.niedobor_sesji = sesja;
        ciag_.maks_czekanie_cpu_ns = czekanie; ciag_.maks_uspienie_ns = uspienie;   // liczniki planisty: sesja
        ++pokolenie_utworu_;
    }
    void zmierz_przerwe_producenta() {
        if (!zegar_) return;
        const uint64_t t = zegar_();
        if (ostatnie_uzupelnienie_ns_ && t > ostatnie_uzupelnienie_ns_) {
            const uint64_t d = t - ostatnie_uzupelnienie_ns_;
            if (d > ciag_.maks_przerwa_producenta_ns) ciag_.maks_przerwa_producenta_ns = d;
            if (d > CZAS_KOLEJKI_NS) ++ciag_.przerwy_ponad_kolejke;
        }
        ostatnie_uzupelnienie_ns_ = t;
    }
    void wyczysc_potok() {
        zr_n_ = zr_poz_ = 0;
        wy_n_ = wy_poz_ = 0;
        koniec_dekodera_ = false;
        finish_wyslany_ = false;
        rs_.reset();
        kor_.reset();
        an_.wyczysc();
    }
    bool przewin_wewn(uint64_t ramka) {
        zamknij_strumien();              // porzuca kolejke: brak starych probek po przewinieciu
        wyczysc_potok();
        if (!dek_->szukaj(ramka)) {
            char b[MAKS_BLAD]; kopiuj_tekst(b, sizeof(b), dek_->ma_blad() ? dek_->blad() : "Nie można przewinąć utworu.");
            return porazka(b);
        }
        baza_wyj_ = Resampler::ramki_wyjscia(dek_->pozycja(), dek_->parametry().czestotliwosc);
        zapisane_ = 0; w_kolejce_ = 0;
        return true;
    }
    void zastosuj_glosnosc() {
        const int64_t r = pcm_.glosnosc(glosnosc_, wyciszony_);
        zastosuj_glosnosc_ = (r == PCM_BUSY);
    }
    void aktualizuj_kolejke() {
        if (!pcm_.otwarty()) return;
        BwsPcmState s{};
        if (pcm_.stan(s) == PCM_OK && s.queued <= s.capacity) w_kolejce_ = s.queued;
    }
    void sprawdz_niedobory() {
        BwsPcmStats st{};
        {
            PomiarSekcji p(Sekcja::Bws110);
            if (pcm_.statystyki(st) != PCM_OK) return;
        }
        if (st.starved > ostatnie_starved_) {
            const uint64_t d = st.starved - ostatnie_starved_;
            ciag_.niedobor_utworu += d;
            ciag_.niedobor_sesji += d;
            ++ciag_.epizody_utworu;
            zdarzenia_ |= ZD_NIEDOBOR;
        }
        if (st.min_queued < ciag_.min_kolejka && st.writes) ciag_.min_kolejka = uint32_t(st.min_queued);
        if (st.max_runnable_wait_ns > ciag_.maks_czekanie_cpu_ns) ciag_.maks_czekanie_cpu_ns = st.max_runnable_wait_ns;
        if (st.max_voluntary_wait_ns > ciag_.maks_uspienie_ns) ciag_.maks_uspienie_ns = st.max_voluntary_wait_ns;
        ostatnie_starved_ = st.starved;
    }
    // Produkuje do PORCJA ramek wyjsciowych w buforze posrednim; false, gdy nic nie powstalo.
    bool wypelnij_posredni() {
        wy_n_ = wy_poz_ = 0;
        while (wy_n_ < PORCJA) {
            if (zr_poz_ >= zr_n_) {
                if (koniec_dekodera_) break;
                zr_poz_ = 0;
                {
                    PomiarSekcji p(Sekcja::Dekodowanie);
                    zr_n_ = dek_->dekoduj(zrodlo_, RAMKI_ZRODLA);
                }
                if (zr_n_ == 0) {
                    if (dek_->ma_blad()) { char b[MAKS_BLAD]; kopiuj_tekst(b, sizeof(b), dek_->blad()); porazka(b); return false; }
                    koniec_dekodera_ = true;
                    break;
                }
            }
            uint32_t zuzyte = 0;
            const uint32_t k = dek_->parametry().kanaly;
            uint32_t n = 0;
            {
                PomiarSekcji p(Sekcja::Konwersja);
                n = rs_.przetworz(zrodlo_ + size_t(zr_poz_) * k, zr_n_ - zr_poz_, &zuzyte,
                                  wyjscie_ + size_t(wy_n_) * 2, PORCJA - wy_n_);
            }
            zr_poz_ += zuzyte;
            wy_n_ += n;
            if (!n && !zuzyte) break;      // ochrona przed petla
        }
        if (!wy_n_) return false;
        { PomiarSekcji p(Sekcja::Korektor); kor_.przetworz(wyjscie_, wy_n_); }
        { PomiarSekcji p(Sekcja::Analiza); an_.dodaj(wyjscie_, wy_n_); }
        return true;
    }
    void uzupelnij(uint32_t prog) {
        BwsPcmState s{};
        int64_t rc;
        { PomiarSekcji p(Sekcja::Bws110); rc = pcm_.stan(s); }
        if (rc == PCM_BUSY) { ++zajete_; return; }
        if (rc != PCM_OK) { porazka("Strumień dźwięku przestał być dostępny."); return; }
        if (s.capacity == 0 || s.queued > s.capacity) { porazka("Niepoprawny stan strumienia dźwięku."); return; }
        w_kolejce_ = s.queued;
        if (finish_wyslany_) {
            if (s.queued == 0) {
                zamknij_strumien();
                baza_wyj_ += zapisane_; zapisane_ = 0; w_kolejce_ = 0;
                od_poczatku_ = true;
                zmien_stan(Stan::Zatrzymany);
                zdarzenia_ |= ZD_KONIEC_UTWORU;
            }
            return;
        }
        uint32_t wolne = s.capacity - s.queued;
        const bool oczekujace = wy_poz_ < wy_n_;
        if (wolne < prog && !(oczekujace && wolne > 0) && s.capacity >= prog) return;
        for (uint32_t i = 0; i < BUDZET_ZAPISOW && wolne > 0; ++i) {
            if (wy_poz_ >= wy_n_ && !wypelnij_posredni()) {
                if (stan_ == Stan::Blad) return;
                break;
            }
            uint32_t n = wy_n_ - wy_poz_;
            if (n > wolne) n = wolne;
            int64_t acc;
            { PomiarSekcji p(Sekcja::Bws110); acc = pcm_.zapisz(wyjscie_ + size_t(wy_poz_) * 2, n); }
            if (acc == PCM_BUSY) { ++zajete_; return; }
            if (acc == PCM_FULL) return;
            if (acc <= 0 || uint64_t(acc) > n) { porazka("Zapis do strumienia dźwięku nie powiódł się."); return; }
            wy_poz_ += uint32_t(acc);
            zapisane_ += uint64_t(acc);
            calkowicie_zapisane_ += uint64_t(acc);
            w_kolejce_ += uint32_t(acc);
            wolne -= uint32_t(acc);
            if (uint32_t(acc) < n) { ++czesciowe_; return; }   // czesciowy: reszta czeka, ponowne QUERY w nast. ticku
        }
        if (koniec_dekodera_ && wy_poz_ >= wy_n_ && !finish_wyslany_) {
            sprawdz_niedobory();            // przed FINISH: niedobory koncowki utworu
            int64_t r;
            { PomiarSekcji p(Sekcja::Bws110); r = pcm_.zakoncz(); }
            if (r == PCM_OK) finish_wyslany_ = true;
        }
    }

    DostepDoPlikow dostep_{};
    StrumienPcm pcm_;
    Dekoder* dek_ = nullptr;
    Resampler rs_;
    Korektor kor_;
    Analizator an_;
    Stan stan_ = Stan::Zatrzymany;
    char blad_[MAKS_BLAD]{};
    char plan_sciezka_[MAKS_SCIEZKA]{};
    FormatAudio plan_format_ = FormatAudio::Nieznany;
    bool plan_odtwarzaj_ = false;
    int16_t zrodlo_[RAMKI_ZRODLA * 2]{};
    uint32_t zr_n_ = 0, zr_poz_ = 0;
    int16_t wyjscie_[PORCJA * 2]{};
    uint32_t wy_n_ = 0, wy_poz_ = 0;
    bool koniec_dekodera_ = false, finish_wyslany_ = false, od_poczatku_ = false;
    uint64_t baza_wyj_ = 0, zapisane_ = 0;
    uint32_t w_kolejce_ = 0;
    uint64_t calkowicie_zapisane_ = 0, ostatnie_starved_ = 0, zajete_ = 0, czesciowe_ = 0;
    StatystykiCiaglosci ciag_{};
    uint32_t pokolenie_utworu_ = 0;
    uint64_t (*zegar_)() = nullptr;
    uint64_t ostatnie_uzupelnienie_ns_ = 0;
    uint32_t licznik_statystyk_ = 0, proby_otwarcia_ = 0;
    uint32_t glosnosc_ = 80;
    bool wyciszony_ = false, zastosuj_glosnosc_ = true;
    uint32_t zdarzenia_ = 0;
};

} // namespace Grajek
