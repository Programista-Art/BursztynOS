#pragma once
/*
 * Menedzer zadan - model danych (bez GUI, bez alokacji, bez libc).
 *
 * Wejscie: migawka BWS 103 (procesy_migawka.h). Model:
 *  - waliduje naglowek i wpisy (wersja, rozmiary, liczby, NUL w sciezce),
 *  - liczy uzycie CPU w PRZEDZIALE miedzy dwiema migawkami: roznica czasu CPU
 *    procesu / roznica czasu rozliczonego (oba liczniki monotoniczne, jadro ich
 *    nie zeruje); procesy dopasowuje para (PID, generacja), wiec ponownie uzyty
 *    PID nigdy nie dziedziczy cudzej historii,
 *  - utrzymuje wybor jako tozsamosc procesu (nie indeks wiersza),
 *  - laczy liczniki I/O (BWS 106) po (PID, generacja) i liczy tempo plikow i sieci w przedziale,
 *  - sortuje (Nazwa, PID, Stan, Procesor, Pamiec, Dysk, Siec, Czas dzialania),
 *  - prowadzi przeplyw "Zakoncz zadanie": potwierdzenie, anulowanie, wynik
 *    jadra, oczekiwanie na faktyczne zwolnienie zasobow,
 *  - przechowuje ograniczona historie (bufor kolowy) do wykresow.
 */
#include <stdint.h>
#include <stddef.h>
#include "procesy_migawka.h"
#include "urzadzenia_telemetria.h"
#include "desktop_entries.h"

namespace MenedzerZadan {

inline constexpr int NIEDOSTEPNE = -1;
inline constexpr uint32_t MAKS = BWS_PROCESY_V2_MAKS;
inline constexpr size_t HISTORIA = 60;      // 60 probek ~ 60 s

/* Stany schedulera (scheduler.h) - kopia wartosci, kontrolowana testem hosta. */
inline constexpr uint32_t ST_PUSTY = 0, ST_GOTOWY = 1, ST_ZABLOKOWANY = 2, ST_MYSZ = 3, ST_ZDARZENIE = 4,
                          ST_KLAWIATURA = 5, ST_KONCZACY = 6;

/* ---------------------------------------------------------------- tekst */
struct Tekst {
    char* b; size_t cap; size_t n = 0;
    Tekst(char* bufor, size_t pojemnosc) : b(bufor), cap(pojemnosc) { if (cap) b[0] = '\0'; }
    // Obciecie przy braku miejsca nigdy nie przecina znaku UTF-8 (etykiety BursztynUI dostaja poprawny tekst).
    void dodaj(const char* s) {
        while (s && *s && n + 1 < cap) b[n++] = *s++;
        if (s && *s && (static_cast<unsigned char>(*s) & 0xC0) == 0x80) {
            while (n > 0 && (static_cast<unsigned char>(b[n - 1]) & 0xC0) == 0x80) --n;
            if (n > 0 && static_cast<unsigned char>(b[n - 1]) >= 0xC0) --n;
        }
        if (cap) b[n] = '\0';
    }
    void znak(char c) { if (n + 1 < cap) { b[n++] = c; b[n] = '\0'; } }
    void liczba(uint64_t v, int min_cyfr = 1) {
        char t[24]; int k = 0;
        do { t[k++] = char('0' + v % 10); v /= 10; } while (v && k < 24);
        while (k < min_cyfr && k < 24) t[k++] = '0';
        while (k) znak(t[--k]);
    }
    // wartosc w dziesiatych czesciach, przecinek dziesietny (polska notacja)
    void dziesietna(uint64_t dziesiate) { liczba(dziesiate / 10); znak(','); liczba(dziesiate % 10); }
};

inline bool rowne(const char* a, const char* b) {
    if (!a || !b) return false;
    size_t i = 0;
    while (a[i] && a[i] == b[i]) ++i;
    return a[i] == b[i];
}

/* Porownanie UTF-8 bez rozrozniania wielkosci liter ASCII i z polskimi literami po bazowych. */
inline uint32_t klucz_znaku(const unsigned char*& s) {
    uint32_t c = *s++;
    if (c >= 0xC0 && (*s & 0xC0) == 0x80) {
        uint32_t d = (c & 0x1F) << 6 | (*s++ & 0x3F);
        switch (d) {
            case 0x104: case 0x105: return 'a' * 4 + 1;
            case 0x106: case 0x107: return 'c' * 4 + 1;
            case 0x118: case 0x119: return 'e' * 4 + 1;
            case 0x141: case 0x142: return 'l' * 4 + 1;
            case 0x143: case 0x144: return 'n' * 4 + 1;
            case 0xD3: case 0xF3: return 'o' * 4 + 1;
            case 0x15A: case 0x15B: return 's' * 4 + 1;
            case 0x179: case 0x17A: return 'z' * 4 + 1;
            case 0x17B: case 0x17C: return 'z' * 4 + 2;
            default: return 0x10000 + d;
        }
    }
    if (c >= 'A' && c <= 'Z') c += 32;
    return c * 4;
}
inline int porownaj_nazwy(const char* a, const char* b) {
    const unsigned char* x = reinterpret_cast<const unsigned char*>(a ? a : "");
    const unsigned char* y = reinterpret_cast<const unsigned char*>(b ? b : "");
    while (*x && *y) {
        const uint32_t kx = klucz_znaku(x), ky = klucz_znaku(y);
        if (kx != ky) return kx < ky ? -1 : 1;
    }
    return *x ? 1 : *y ? -1 : 0;
}

/* -------------------------------------------------------- nazwy procesow */
struct NazwaSystemowa { const char* sciezka; const char* nazwa; };
inline constexpr NazwaSystemowa NAZWY_SYSTEMOWE[] = {
    {"/menedzer_okien.bur", "Menedżer okien (pulpit)"},
    {"/programy/menedzer_okien.cebula/menedzer_okien.bur", "Menedżer okien (pulpit)"},
    {"/shell.bur", "Powłoka Bursztyna"},
    {"/programy/tapety.cebula/tapety.bur", "Tapety"},
    {"/programy/okno-informacyjne.cebula/okno-informacyjne.bur", "Okno informacyjne"},
    {"/programy/husarz-worker.cebula/husarz-worker.bur", "Husarz (proces roboczy)"},
    {"/programy/menedzer_zadan.cebula/menedzer_zadan.bur", "Menedżer zadań"},
};

/* Czytelna nazwa: rejestr skrotow pulpitu (ta sama tozsamosc co ikony), potem nazwy systemowe, potem plik. */
inline void nazwa_procesu(uint32_t pid, const char* sciezka, char* out, size_t cap) {
    Tekst t(out, cap);
    if (pid == 0) { t.dodaj("Jądro systemu"); return; }
    if (!sciezka || !sciezka[0]) { t.dodaj("(bez nazwy)"); return; }
    for (const auto& l : DesktopEntries::Launchers)
        if (rowne(l.path, sciezka)) { t.dodaj(l.name); return; }
    for (const auto& s : NAZWY_SYSTEMOWE)
        if (rowne(s.sciezka, sciezka)) { t.dodaj(s.nazwa); return; }
    size_t dl = 0; while (sciezka[dl]) ++dl;
    size_t start = dl; while (start > 0 && sciezka[start - 1] != '/') --start;
    size_t koniec = dl;
    if (koniec - start > 4 && rowne(sciezka + koniec - 4, ".bur")) koniec -= 4;
    for (size_t i = start; i < koniec; ++i) t.znak(sciezka[i]);
    if (!t.n) t.dodaj("(bez nazwy)");
}

inline const char* nazwa_stanu(uint32_t stan, bool wywolujacy) {
    switch (stan) {
        case ST_GOTOWY: return wywolujacy ? "Uruchomiony" : "Gotowy";
        case ST_ZABLOKOWANY: return "Uruchamianie";
        case ST_MYSZ: return "Oczekujący (mysz)";
        case ST_ZDARZENIE: return "Oczekujący (zdarzenia)";
        case ST_KLAWIATURA: return "Oczekujący (klawiatura)";
        case ST_KONCZACY: return "Kończący";
        default: return "Nieznany";
    }
}

inline const char* powod_ochrony(uint32_t ochrona) {
    switch (ochrona) {
        case BWS_OCHRONA_BRAK: return "";
        case BWS_OCHRONA_JADRO: return "Jądro systemu (PID 0) nie może zostać zakończone.";
        case BWS_OCHRONA_MENEDZER_OKIEN: return "Menedżer okien jest procesem chronionym - jego zakończenie zamknęłoby pulpit.";
        case BWS_OCHRONA_USLUGA: return "Usługa systemowa o wyższym poziomie zaufania (PZB) - zakończenie zablokowane.";
        case BWS_OCHRONA_WYZSZE_ZAUFANIE: return "Proces ma wyższy poziom zaufania niż Menedżer zadań.";
        case BWS_OCHRONA_WLASNY: return "To okno Menedżera zadań - zamknij je zwykłym przyciskiem.";
        case BWS_OCHRONA_URUCHAMIANY: return "Proces jest właśnie uruchamiany - spróbuj za chwilę.";
        case BWS_OCHRONA_KONCZACY: return "Proces jest już kończony - trwa zwalnianie zasobów.";
        default: return "Proces chroniony.";
    }
}

/* Krotka wersja do paska stanu obok przyciskow (pelne zdanie trafia do dialogu). */
inline const char* krotki_powod_ochrony(uint32_t ochrona) {
    switch (ochrona) {
        case BWS_OCHRONA_BRAK: return "";
        case BWS_OCHRONA_JADRO: return "Chroniony: jądro systemu";
        case BWS_OCHRONA_MENEDZER_OKIEN: return "Chroniony: Menedżer okien";
        case BWS_OCHRONA_USLUGA: return "Chroniony: usługa systemowa";
        case BWS_OCHRONA_WYZSZE_ZAUFANIE: return "Chroniony: wyższe zaufanie (PZB)";
        case BWS_OCHRONA_WLASNY: return "To okno Menedżera zadań";
        case BWS_OCHRONA_URUCHAMIANY: return "Proces jest uruchamiany";
        case BWS_OCHRONA_KONCZACY: return "Proces jest już kończony";
        default: return "Proces chroniony";
    }
}

inline const char* opis_wyniku_zakonczenia(uint64_t w) {
    switch (w) {
        case BWS_ZAKONCZ_ZAAKCEPTOWANO: return "Jądro przyjęło żądanie zakończenia. Trwa zwalnianie zasobów procesu.";
        case BWS_ZAKONCZ_JUZ_KONCZONY: return "Proces był już kończony. Trwa zwalnianie zasobów.";
        case BWS_ZAKONCZ_BRAK_PROCESU: return "Proces już nie istnieje (zakończył się albo jego PID został ponownie użyty) - nic nie zakończono.";
        case BWS_ZAKONCZ_BRAK_UPRAWNIEN: return "Brak uprawnień do kończenia procesów.";
        case BWS_ZAKONCZ_CHRONIONY: return "Jądro odmówiło: proces jest chroniony.";
        case BWS_ZAKONCZ_WLASNY: return "Menedżer zadań nie kończy samego siebie - zamknij okno zwykłym przyciskiem.";
        case BWS_ZAKONCZ_URUCHAMIANY: return "Proces jest jeszcze uruchamiany - spróbuj za chwilę.";
        case BWS_ZAKONCZ_ZLE_ARGUMENTY: return "Niepoprawne żądanie zakończenia.";
        default: return "Zakończenie nie powiodło się (nieznana odpowiedź jądra).";
    }
}

/* ------------------------------------------------------------ formatowanie */
inline void format_pamieci(uint64_t bajty, char* out, size_t cap) {
    Tekst t(out, cap);
    const uint64_t kib = (bajty + 512) / 1024;
    if (bajty < 1024ULL * 1024ULL) { t.liczba(kib); t.dodaj(" KiB"); return; }
    if (bajty < 1024ULL * 1024ULL * 1024ULL) { t.dziesietna((bajty * 10 + 512 * 1024) / (1024 * 1024)); t.dodaj(" MiB"); return; }
    t.dziesietna((bajty * 10 + 512ULL * 1024 * 1024) / (1024ULL * 1024 * 1024)); t.dodaj(" GiB");
}
inline void format_procentu(int promile, char* out, size_t cap) {
    Tekst t(out, cap);
    if (promile < 0) { t.dodaj("Niedostępne"); return; }
    t.dziesietna(static_cast<uint64_t>(promile)); t.dodaj(" %");
}
inline void format_czasu(uint64_t ns, char* out, size_t cap) {
    Tekst t(out, cap);
    const uint64_t s = ns / 1000000000ULL;
    if (s >= 3600) { t.liczba(s / 3600); t.dodaj(" h "); t.liczba((s / 60) % 60, 2); t.dodaj(" min"); }
    else if (s >= 60) { t.liczba(s / 60); t.dodaj(" min "); t.liczba(s % 60, 2); t.dodaj(" s"); }
    else { t.liczba(s); t.dodaj(" s"); }
}

/* Promile z ulamka; mianownik 0 albo licznik > mianownik o wiecej niz zaokraglenie -> obciecie do 100 %. */
inline int promile(uint64_t licznik, uint64_t mianownik) {
    if (mianownik == 0) return NIEDOSTEPNE;
    if (licznik >= mianownik) return 1000;
    // Bez dzielenia 128-bitowego (Ring 3 freestanding): skalowanie obu wartosci zachowuje proporcje.
    while (licznik > UINT64_MAX / 1000U) { licznik >>= 1; mianownik >>= 1; }
    if (mianownik == 0) return NIEDOSTEPNE;
    return static_cast<int>((licznik * 1000U + mianownik / 2) / mianownik);
}

/* ----------------------------------------------------------------- dane */
struct Tozsamosc {
    static constexpr uint32_t BRAK = 0xFFFFFFFFU;   // PID 0 ma generacje 0, wiec {0,0} to prawdziwy proces (jadro)
    uint32_t pid = BRAK; uint64_t generacja = 0;
    bool pusta() const { return pid == BRAK; }
    bool operator==(const Tozsamosc& o) const { return pid == o.pid && generacja == o.generacja; }
};

struct Wiersz {
    uint32_t pid, stan, poziom, flagi, ochrona;
    uint64_t generacja, cpu_ns, start_ns, uprawnienia;
    uint64_t prywatna, tablice, wspoldzielona, stos, okno, sterta;
    int cpu_promile;                 // NIEDOSTEPNE, gdy brak przedzialu pomiaru
    /* I/O (BWS 106). Tempa w B/s; -1 = niedostepne (brak przedzialu, brak licznika, licznik nie dotyczy). */
    bool io_znane;                   // liczniki skumulowane ponizej pochodza z jadra
    uint32_t io_flagi;               // BWS_IO_PLIKI / BWS_IO_SIEC
    uint64_t pliki_odczyt, pliki_zapis, siec_wyslane, siec_odebrane;
    int64_t dysk_bps, siec_bps, pliki_odczyt_bps, pliki_zapis_bps, siec_wyslane_bps, siec_odebrane_bps;
    char sciezka[BWS_PROCES_SCIEZKA_MAKS];
    char nazwa[64];
    Tozsamosc id() const { return {pid, generacja}; }
    bool pamiec_zmierzona() const { return (flagi & BWS_PROC_PAMIEC_ZMIERZONA) != 0; }
    // Pamiec przypisana procesowi: wszystko, co jadro zwalnia przy jego zakonczeniu (bez SHM).
    uint64_t pamiec() const { return pamiec_zmierzona() ? prywatna + tablice + stos + okno : okno; }
    bool mozna_zakonczyc() const { return (flagi & BWS_PROC_MOZNA_ZAKONCZYC) != 0 && ochrona == BWS_OCHRONA_BRAK; }
    bool wywolujacy() const { return (flagi & BWS_PROC_WYWOLUJACY) != 0; }
};

struct System {
    bool cpu_dostepne = false;       // jest poprzednia migawka i zrodlo czasu
    int cpu_promile = NIEDOSTEPNE;   // (przedzial - bezczynnosc) / przedzial
    int jadro_promile = NIEDOSTEPNE; // PID 0 (praca jadra, bez HLT)
    int bezczynnosc_promile = NIEDOSTEPNE;
    bool hpet = false, ticki = false;
    uint64_t okres_ticku_ns = 0, przedzial_ns = 0;
    bool ram_dostepna = false;
    uint64_t ram_zarzadzana = 0, ram_wolna = 0, ram_uzywana = 0, ram_firmware = 0;
    uint64_t ram_procesow = 0;       // suma pamieci przypisanej procesom
    uint64_t ram_reszta = 0;         // uzywana - procesy: jadro, bufory, SHM, pamiec podreczna
    int ram_promile = NIEDOSTEPNE;
    uint32_t procesow = 0;
    bool obcieta = false;
};

enum class Kolumna : uint8_t { Nazwa = 0, Pid = 1, Stan = 2, Procesor = 3, Pamiec = 4, Dysk = 5, Siec = 6, Czas = 7 };
inline constexpr int LICZBA_KOLUMN = 8;

/* Tempo I/O jednej generacji procesu w ostatnim przedziale BWS 106. */
struct ProcesIo {
    Tozsamosc id;
    uint32_t flagi = 0;
    uint64_t v[4]{};                 // pliki odczyt, pliki zapis, siec wyslane, siec odebrane (skumulowane)
    int64_t bps[4] = {-1, -1, -1, -1};
};

/* Przeplyw zakonczenia */
enum class Etap : uint8_t { Brak, Potwierdzanie, Oczekiwanie };
enum class Prosba : uint8_t { BrakWyboru, Chroniony, ZamknijSiebie, Potwierdz };
enum class Zdarzenie : uint8_t { Nic, Zakonczony, NadalKonczony, PrzekroczonyCzas };

class Model {
public:
    /*
     * Liczniki I/O (BWS 106) - wolac PRZED zastosuj() tej samej probki: zastosuj() wpisuje tempa do wierszy
     * przed sortowaniem. Tempo = roznica licznikow tej samej (PID, generacja) / roznica czas_ns. Generacja,
     * ktorej nie bylo w poprzedniej migawce, powstala w tym przedziale i zaczela od zera: tempo = licznik / przedzial.
     * false = odpowiedz odrzucona (tempa niedostepne, nic nie jest zgadywane).
     */
    bool zastosuj_io(const BwsProcesyIoOdpowiedz& o) {
        const auto& n = o.naglowek;
        bool ok = n.wersja == BWS_PROCESY_IO_V1_ABI && n.rozmiar_naglowka == sizeof(BwsProcesyIoNaglowek) &&
                  n.rozmiar_wpisu == sizeof(BwsProcesIoV1) && n.liczba_wpisow <= BWS_IO_MAKS_PROCESOW;
        for (uint32_t i = 0; ok && i < n.liczba_wpisow; ++i) {
            if (o.wpisy[i].pid >= MAKS) ok = false;
            for (uint32_t j = 0; ok && j < i; ++j) if (o.wpisy[j].pid == o.wpisy[i].pid) ok = false;
        }
        if (!ok) { io_liczba_ = 0; io_poprz_ = false; return false; }
        const bool zegar = (n.flagi & BWS_IO_ZEGAR) && n.czas_ns;
        const uint64_t dt = zegar && io_poprz_ && n.czas_ns > io_poprz_ns_ ? n.czas_ns - io_poprz_ns_ : 0;
        ProcesIo nowe[BWS_IO_MAKS_PROCESOW];
        for (uint32_t i = 0; i < n.liczba_wpisow; ++i) {
            const BwsProcesIoV1& w = o.wpisy[i];
            ProcesIo& r = nowe[i];
            r.id = {w.pid, w.generacja}; r.flagi = w.flagi;
            r.v[0] = w.pliki_odczyt_bajtow; r.v[1] = w.pliki_zapis_bajtow;
            r.v[2] = w.siec_wyslane_bajtow; r.v[3] = w.siec_odebrane_bajtow;
            if (!dt) continue;
            const ProcesIo* s = nullptr;
            for (uint32_t j = 0; j < io_liczba_; ++j) if (io_[j].id == r.id) s = &io_[j];
            for (int k = 0; k < 4; ++k) {
                const uint64_t przed = s ? s->v[k] : 0;
                if (r.v[k] < przed) continue;                   // spadek = niespojnosc: niedostepne
                const uint64_t d = r.v[k] - przed;
                // bajty * 1e9 / dt bez przepelnienia: d < 2^34 B w 1 s jest zawsze bezpieczne
                r.bps[k] = d <= UINT64_MAX / 1000000000ULL ? static_cast<int64_t>((d * 1000000000ULL + dt / 2) / dt)
                                                           : static_cast<int64_t>(d / (dt / 1000000000ULL + 1));
            }
        }
        for (uint32_t i = 0; i < n.liczba_wpisow; ++i) io_[i] = nowe[i];
        io_liczba_ = n.liczba_wpisow;
        io_naglowek_ = n;
        io_poprz_ = zegar;
        io_poprz_ns_ = zegar ? n.czas_ns : 0;
        return true;
    }
    void blad_io() { io_liczba_ = 0; io_poprz_ = false; }
    const BwsProcesyIoNaglowek& naglowek_io() const { return io_naglowek_; }

    /* Zastosuj migawke. false = odpowiedz niepoprawna (dane poprzednie pozostaja, nic nie jest wymyslane). */
    bool zastosuj(const BwsProcesyNaglowek& n, const BwsProcesInfoV2* wpisy, uint64_t teraz_monotoniczny_ns = 0) {
        if (n.wersja != BWS_PROCESY_V2_ABI || n.rozmiar_naglowka != sizeof(BwsProcesyNaglowek) ||
            n.rozmiar_wpisu != sizeof(BwsProcesInfoV2) || n.liczba_wpisow > MAKS ||
            n.liczba_procesow < n.liczba_wpisow || (!wpisy && n.liczba_wpisow)) return false;
        for (uint32_t i = 0; i < n.liczba_wpisow; ++i) {
            const auto& w = wpisy[i];
            if (w.pid >= MAKS || w.stan > ST_KONCZACY || (w.pid != 0 && w.stan == ST_PUSTY)) return false;
            for (uint32_t j = 0; j < i; ++j) if (wpisy[j].pid == w.pid) return false;   // duplikat PID
        }
        // Czas rozliczony jest monotoniczny; spadek oznacza blad zrodla - przedzial niewazny.
        const bool ma_poprzednia = poprzednia_ && (n.flagi & (BWS_MIGAWKA_CPU_HPET | BWS_MIGAWKA_CPU_TICKI)) &&
                                   n.czas_rozliczony_ns > poprz_rozliczony_ && n.czas_bezczynnosci_ns >= poprz_bezczynnosc_;
        const uint64_t przedzial = ma_poprzednia ? n.czas_rozliczony_ns - poprz_rozliczony_ : 0;

        Wiersz nowe[MAKS]{};
        uint32_t liczba = 0;
        for (uint32_t i = 0; i < n.liczba_wpisow; ++i) {
            const auto& w = wpisy[i];
            Wiersz& r = nowe[liczba++];
            r.pid = w.pid; r.stan = w.stan; r.poziom = w.poziom_pzb; r.flagi = w.flagi; r.ochrona = w.ochrona;
            r.generacja = w.generacja; r.cpu_ns = w.czas_cpu_ns; r.start_ns = w.czas_startu_ns;
            r.uprawnienia = w.uprawnienia;
            r.prywatna = w.pamiec_prywatna_bajtow; r.tablice = w.pamiec_tablic_stron_bajtow;
            r.wspoldzielona = w.pamiec_wspoldzielona_bajtow; r.stos = w.stos_jadra_bajtow;
            r.okno = w.bufor_okna_bajtow; r.sterta = w.sterta_wirtualna_bajtow;
            size_t k = 0;
            for (; k + 1 < sizeof(r.sciezka) && w.sciezka[k]; ++k) r.sciezka[k] = w.sciezka[k];
            r.sciezka[k] = '\0';
            nazwa_procesu(r.pid, r.sciezka, r.nazwa, sizeof(r.nazwa));
            wpisz_io(r);
            r.cpu_promile = NIEDOSTEPNE;
            if (!przedzial) continue;
            const Wiersz* stary = znajdz_w(poprz_, poprz_liczba_, r.id());
            if (stary) {
                if (r.cpu_ns >= stary->cpu_ns) r.cpu_promile = promile(r.cpu_ns - stary->cpu_ns, przedzial);
            } else if (r.pid != 0 && r.start_ns >= poprz_czas_ && r.start_ns <= n.czas_ns) {
                r.cpu_promile = promile(r.cpu_ns, przedzial);   // proces powstal w tym przedziale
            }
        }

        // System
        System s{};
        s.hpet = (n.flagi & BWS_MIGAWKA_CPU_HPET) != 0;
        s.ticki = (n.flagi & BWS_MIGAWKA_CPU_TICKI) != 0;
        s.okres_ticku_ns = n.okres_ticku_ns;
        s.przedzial_ns = przedzial;
        s.procesow = n.liczba_procesow;
        s.obcieta = (n.flagi & BWS_MIGAWKA_OBCIETA) != 0;
        if (przedzial) {
            const uint64_t bezcz = n.czas_bezczynnosci_ns - poprz_bezczynnosc_;
            s.cpu_dostepne = true;
            s.bezczynnosc_promile = promile(bezcz, przedzial);
            s.cpu_promile = 1000 - s.bezczynnosc_promile;
            for (uint32_t i = 0; i < liczba; ++i) if (nowe[i].pid == 0) s.jadro_promile = nowe[i].cpu_promile;
        }
        if ((n.flagi & BWS_MIGAWKA_RAM) && n.ram_zarzadzana_bajtow && n.ram_wolna_bajtow <= n.ram_zarzadzana_bajtow) {
            s.ram_dostepna = true;
            s.ram_zarzadzana = n.ram_zarzadzana_bajtow;
            s.ram_wolna = n.ram_wolna_bajtow;
            s.ram_uzywana = n.ram_zarzadzana_bajtow - n.ram_wolna_bajtow;
            for (uint32_t i = 0; i < liczba; ++i) s.ram_procesow += nowe[i].pamiec();
            s.ram_reszta = s.ram_uzywana > s.ram_procesow ? s.ram_uzywana - s.ram_procesow : 0;
            s.ram_promile = promile(s.ram_uzywana, s.ram_zarzadzana);
        }
        s.ram_firmware = n.ram_firmware_bajtow;

        // Publikacja (stare -> poprzednie)
        for (uint32_t i = 0; i < liczba; ++i) poprz_[i] = nowe[i];
        poprz_liczba_ = liczba;
        for (uint32_t i = 0; i < liczba; ++i) wiersze_[i] = nowe[i];
        liczba_ = liczba;
        poprz_rozliczony_ = n.czas_rozliczony_ns;
        poprz_bezczynnosc_ = n.czas_bezczynnosci_ns;
        poprz_czas_ = n.czas_ns;
        poprzednia_ = (n.flagi & (BWS_MIGAWKA_CPU_HPET | BWS_MIGAWKA_CPU_TICKI)) != 0;
        system_ = s;
        wlasny_ = {n.pid_wywolujacego, n.generacja_wywolujacego};
        if (s.cpu_dostepne || s.ram_dostepna) dopisz_historie(s.cpu_promile, s.ram_promile);
        sortuj();
        ostatnie_zdarzenie_ = sprawdz_zakonczenie(teraz_monotoniczny_ns);
        if (!wybor_.pusta() && indeks(wybor_) < 0) { wybor_ = {}; wybor_zniknal_ = true; }
        return true;
    }

    /* Odpowiedz BWS 103 z bledem: dane nie sa aktualizowane, przedzial CPU zaczyna sie od nowa. */
    void blad_odczytu() { poprzednia_ = false; }

    // --------------------------------------------------------------- widok
    uint32_t liczba() const { return liczba_; }
    const Wiersz* wiersz(uint32_t i) const { return i < liczba_ ? &wiersze_[i] : nullptr; }
    const System& system() const { return system_; }
    Tozsamosc wlasny() const { return wlasny_; }
    uint64_t ostatni_czas_ns() const { return poprz_czas_; }   // zegar jadra ostatniej migawki (czas dzialania)
    int indeks(const Tozsamosc& t) const {
        for (uint32_t i = 0; i < liczba_; ++i) if (wiersze_[i].id() == t) return static_cast<int>(i);
        return -1;
    }

    // --------------------------------------------------------------- sortowanie
    void ustaw_sortowanie(Kolumna k, bool rosnaco) { kolumna_ = k; rosnaco_ = rosnaco; sortuj(); }
    Kolumna kolumna() const { return kolumna_; }
    bool rosnaco() const { return rosnaco_; }

    // --------------------------------------------------------------- wybor
    void wybierz_indeks(uint32_t i) { if (i < liczba_) { wybor_ = wiersze_[i].id(); wybor_zniknal_ = false; } }
    void wyczysc_wybor() { wybor_ = {}; }
    Tozsamosc wybor() const { return wybor_; }
    int indeks_wyboru() const { return wybor_.pusta() ? -1 : indeks(wybor_); }
    const Wiersz* wybrany() const { const int i = indeks_wyboru(); return i < 0 ? nullptr : &wiersze_[i]; }
    bool wybor_zniknal() { const bool z = wybor_zniknal_; wybor_zniknal_ = false; return z; }

    // --------------------------------------------------------------- zakonczenie
    /* Klik "Zakoncz zadanie": co pokazac. Przy Potwierdz zapamietuje DOKLADNY cel (tozsamosc + nazwe). */
    Prosba zadaj_zakonczenia() {
        const Wiersz* w = wybrany();
        if (!w) return Prosba::BrakWyboru;
        if (w->wywolujacy() || w->id() == wlasny_) return Prosba::ZamknijSiebie;
        if (!w->mozna_zakonczyc()) return Prosba::Chroniony;
        etap_ = Etap::Potwierdzanie;
        cel_ = w->id();
        Tekst t(cel_nazwa_, sizeof(cel_nazwa_)); t.dodaj(w->nazwa);
        return Prosba::Potwierdz;
    }
    void anuluj() { if (etap_ == Etap::Potwierdzanie) { etap_ = Etap::Brak; cel_ = {}; } }
    /* Potwierdzenie: jedno wywolanie jadra z zapamietana tozsamoscia (nie z biezacym wyborem). */
    template <class Zakoncz>
    uint64_t potwierdz(Zakoncz&& zakoncz, uint64_t teraz_ns) {
        if (etap_ != Etap::Potwierdzanie || cel_.generacja == 0) return BWS_ZAKONCZ_ZLE_ARGUMENTY;
        const uint64_t wynik = zakoncz(cel_.pid, cel_.generacja);
        if (wynik == BWS_ZAKONCZ_ZAAKCEPTOWANO || wynik == BWS_ZAKONCZ_JUZ_KONCZONY) {
            etap_ = Etap::Oczekiwanie; od_ns_ = teraz_ns;
        } else { etap_ = Etap::Brak; cel_ = {}; }
        return wynik;
    }
    Etap etap() const { return etap_; }
    Tozsamosc cel() const { return cel_; }
    const char* cel_nazwa() const { return cel_nazwa_; }
    Zdarzenie zdarzenie() { const Zdarzenie z = ostatnie_zdarzenie_; ostatnie_zdarzenie_ = Zdarzenie::Nic; return z; }

    // --------------------------------------------------------------- historia
    size_t historia(int* cpu, int* ram, size_t cap) const {
        size_t n = liczba_historii_ < cap ? liczba_historii_ : cap;
        for (size_t i = 0; i < n; ++i) {
            const size_t idx = (glowa_historii_ + HISTORIA - n + i) % HISTORIA;   // n najnowszych, od najstarszej
            if (cpu) cpu[i] = hist_cpu_[idx];
            if (ram) ram[i] = hist_ram_[idx];
        }
        return n;
    }

private:
    /* Wiersz dostaje liczniki tylko z wpisu tej samej (PID, generacja). PID 0 nie uzywa wywolan plikowych. */
    void wpisz_io(Wiersz& r) const {
        r.io_znane = false; r.io_flagi = 0;
        r.pliki_odczyt = r.pliki_zapis = r.siec_wyslane = r.siec_odebrane = 0;
        r.dysk_bps = r.siec_bps = r.pliki_odczyt_bps = r.pliki_zapis_bps = r.siec_wyslane_bps = r.siec_odebrane_bps = -1;
        for (uint32_t i = 0; i < io_liczba_; ++i) {
            const ProcesIo& p = io_[i];
            if (!(p.id == r.id())) continue;
            r.io_znane = true; r.io_flagi = p.flagi;
            r.pliki_odczyt = p.v[0]; r.pliki_zapis = p.v[1]; r.siec_wyslane = p.v[2]; r.siec_odebrane = p.v[3];
            if (p.flagi & BWS_IO_PLIKI) {
                r.pliki_odczyt_bps = p.bps[0]; r.pliki_zapis_bps = p.bps[1];
                if (p.bps[0] >= 0 && p.bps[1] >= 0) r.dysk_bps = p.bps[0] + p.bps[1];
            }
            if (p.flagi & BWS_IO_SIEC) {
                r.siec_wyslane_bps = p.bps[2]; r.siec_odebrane_bps = p.bps[3];
                if (p.bps[2] >= 0 && p.bps[3] >= 0) r.siec_bps = p.bps[2] + p.bps[3];
            }
            return;
        }
    }
    static const Wiersz* znajdz_w(const Wiersz* t, uint32_t n, const Tozsamosc& id) {
        for (uint32_t i = 0; i < n; ++i) if (t[i].id() == id) return &t[i];
        return nullptr;
    }
    int porownaj(const Wiersz& a, const Wiersz& b) const {
        int c = 0;
        switch (kolumna_) {
            case Kolumna::Nazwa: c = porownaj_nazwy(a.nazwa, b.nazwa); break;
            case Kolumna::Pid: c = 0; break;
            case Kolumna::Stan: c = a.stan < b.stan ? -1 : a.stan > b.stan ? 1 : 0; break;
            case Kolumna::Procesor: c = a.cpu_promile < b.cpu_promile ? -1 : a.cpu_promile > b.cpu_promile ? 1 : 0; break;
            case Kolumna::Pamiec: c = a.pamiec() < b.pamiec() ? -1 : a.pamiec() > b.pamiec() ? 1 : 0; break;
            case Kolumna::Dysk: c = a.dysk_bps < b.dysk_bps ? -1 : a.dysk_bps > b.dysk_bps ? 1 : 0; break;
            case Kolumna::Siec: c = a.siec_bps < b.siec_bps ? -1 : a.siec_bps > b.siec_bps ? 1 : 0; break;
            // Dluzej dzialajacy = wczesniejszy start; PID 0 (bez czasu startu) na koncu rosnaco
            case Kolumna::Czas: {
                const uint64_t sa = a.start_ns ? a.start_ns : UINT64_MAX, sb = b.start_ns ? b.start_ns : UINT64_MAX;
                c = sa > sb ? -1 : sa < sb ? 1 : 0; break;
            }
        }
        if (c == 0) c = a.pid < b.pid ? -1 : a.pid > b.pid ? 1 : 0;   // deterministycznie: PID
        return rosnaco_ ? c : -c;
    }
    void sortuj() {
        for (uint32_t i = 1; i < liczba_; ++i) {
            Wiersz x = wiersze_[i];
            uint32_t j = i;
            while (j > 0 && porownaj(x, wiersze_[j - 1]) < 0) { wiersze_[j] = wiersze_[j - 1]; --j; }
            wiersze_[j] = x;
        }
    }
    void dopisz_historie(int cpu, int ram) {
        hist_cpu_[glowa_historii_] = cpu < 0 ? 0 : cpu;
        hist_ram_[glowa_historii_] = ram < 0 ? 0 : ram;
        glowa_historii_ = (glowa_historii_ + 1) % HISTORIA;
        if (liczba_historii_ < HISTORIA) ++liczba_historii_;
    }
    Zdarzenie sprawdz_zakonczenie(uint64_t teraz) {
        if (etap_ != Etap::Oczekiwanie) return Zdarzenie::Nic;
        const int i = indeks(cel_);
        if (i < 0) { etap_ = Etap::Brak; cel_ = {}; return Zdarzenie::Zakonczony; }   // slot zwolniony / inna generacja
        if (teraz && teraz > od_ns_ && teraz - od_ns_ > 10000000000ULL) { etap_ = Etap::Brak; cel_ = {}; return Zdarzenie::PrzekroczonyCzas; }
        return Zdarzenie::NadalKonczony;
    }

    Wiersz wiersze_[MAKS]{};
    uint32_t liczba_ = 0;
    Wiersz poprz_[MAKS]{};
    uint32_t poprz_liczba_ = 0;
    bool poprzednia_ = false;
    uint64_t poprz_rozliczony_ = 0, poprz_bezczynnosc_ = 0, poprz_czas_ = 0;
    System system_{};
    Tozsamosc wlasny_{}, wybor_{}, cel_{};
    bool wybor_zniknal_ = false;
    Kolumna kolumna_ = Kolumna::Nazwa;
    bool rosnaco_ = true;
    Etap etap_ = Etap::Brak;
    char cel_nazwa_[64]{};
    uint64_t od_ns_ = 0;
    Zdarzenie ostatnie_zdarzenie_ = Zdarzenie::Nic;
    int hist_cpu_[HISTORIA]{}, hist_ram_[HISTORIA]{};
    ProcesIo io_[BWS_IO_MAKS_PROCESOW];
    uint32_t io_liczba_ = 0;
    bool io_poprz_ = false;
    uint64_t io_poprz_ns_ = 0;
    BwsProcesyIoNaglowek io_naglowek_{};
    size_t glowa_historii_ = 0, liczba_historii_ = 0;
};

}  // namespace MenedzerZadan
