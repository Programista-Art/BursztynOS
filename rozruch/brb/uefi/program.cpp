#include "program.h"

#include "../wspolne/pamiec_podstawowa.h"
#include "../wspolne/utf8.h"
#include "../wspolne/wersja.h"

namespace brb {

namespace {

constexpr EFI_GUID GUID_LOADED_IMAGE = EFI_LOADED_IMAGE_PROTOCOL_GUID;
constexpr size_t POJEMNOSC_LINII = 160;   // jednostek CHAR16 na jedna linie ekranu (z CR LF i NUL)
constexpr UINT32 MAKS_OPROZNIANIE_BUFORA = 32;
constexpr UINT32 MAKS_POBUDZEN_BEZ_KLAWISZA = 16;

// Kontrola relokacji: wskaznik bezwzgledny w danych. Konsolidator musi dla niego utworzyc wpis relokacji
// bazowej (IMAGE_REL_BASED_DIR64), a firmware - zastosowac go przy ladowaniu pod inny adres niz ImageBase.
// volatile: kompilator nie moze zwinac porownania do stalej.
const char ZNACZNIK_RELOKACJI[] = "BRB-relokacja";
const char* const volatile WSKAZNIK_RELOKACJI = ZNACZNIK_RELOKACJI;

struct Konsola {
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* wy;
    bool transliteracja;
    UINT32 ostrzezenia;
};

void log_status(LogSzeregowy& log, const char* prefiks, EFI_STATUS s) {
    log.tekst(prefiks);
    log.tekst(" status=");
    log.hex(s);
    log.tekst(" (");
    log.tekst(nazwa_statusu(s));
    log.linia(")");
}

// Wypisuje jedna linie UTF-8 (+ CR LF). Zwraca blad firmware albo EFI_SUCCESS. Ostrzezenie
// EFI_WARN_UNKNOWN_GLYPH nie jest bledem (UEFI 2.10, 12.4 OutputString) - jest liczone.
EFI_STATUS wypisz(Konsola& k, const char* utf8) {
    char16_t bufor[POJEMNOSC_LINII];
    const WynikUtf8 w = utf8_na_ucs2(utf8, dlugosc_tekstu(utf8, POJEMNOSC_LINII * 4), bufor, POJEMNOSC_LINII - 2);
    size_t n = w.zapisane;
    bufor[n++] = u'\r';
    bufor[n++] = u'\n';
    bufor[n] = 0;
    if (k.transliteracja) transliteruj_ascii(bufor, POJEMNOSC_LINII);
    const EFI_STATUS s = k.wy->OutputString(k.wy, bufor);
    if (s == EFI_WARN_UNKNOWN_GLYPH) { ++k.ostrzezenia; return EFI_SUCCESS; }
    return s;
}

void ustaw_kolor(Konsola& k, LogSzeregowy& log, UINTN atrybut) {
    if (!k.wy->SetAttribute) return;
    const EFI_STATUS s = k.wy->SetAttribute(k.wy, atrybut);
    if (EFI_ERROR(s)) log_status(log, "[BRB] konsola: SetAttribute nieudane (kontynuuje)", s);
}

// Polityka polskich znakow na konsoli tekstowej firmware (docs/brb/architektura-uefi.md, sekcja 14.4):
// TestString/EFI_WARN_UNKNOWN_GLYPH nie sa wiarygodne - OVMF 2024.02 zglasza EFI_SUCCESS nawet dla U+4E2D,
// a jego konsola graficzna pomija litery spoza Latin-1 (ą ć ę ł ń ś ź ż). Dlatego domyslnie transliteracja
// ASCII; pelne polskie znaki tylko na jawne zadanie (polskie_znaki_konsoli) i tylko gdy TestString nie
// zaprzecza. Prawdziwe polskie glify wymagaja wlasnej czcionki na GOP (pozniejsza faza).
bool potrzebna_transliteracja(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* wy, LogSzeregowy& log, const Ustawienia& u) {
    if (!u.polskie_znaki_konsoli) {
        log.linia("[BRB] konsola: transliteracja ASCII (czcionka firmware nie gwarantuje polskich liter)");
        return true;
    }
    if (!wy->TestString) {
        log.linia("[BRB] konsola: brak TestString - transliteracja ASCII");
        return true;
    }
    char16_t probka[24];
    utf8_na_ucs2("ąćęłńóśźżĄĆĘŁŃÓŚŹŻ", 36, probka, 24);
    const EFI_STATUS s = wy->TestString(wy, probka);
    if (s == EFI_SUCCESS) return false;
    log_status(log, "[BRB] konsola: brak polskich glifow - transliteracja ASCII", s);
    return true;
}

// Ograniczony bufor tekstu (UTF-8) do skladania linii ekranu; przepelnienie = obciecie, zawsze z NUL.
struct Tekst {
    char b[POJEMNOSC_LINII];
    size_t n = 0;
    Tekst() { b[0] = '\0'; }
    Tekst& dodaj(const char* t) {
        for (size_t i = 0; t && t[i] && n + 1 < sizeof(b); ++i) b[n++] = t[i];
        b[n] = '\0';
        return *this;
    }
    Tekst& dziesietnie(uint64_t v) {
        char c[20];
        int k = 0;
        do { c[k++] = char('0' + v % 10); v /= 10; } while (v && k < 20);
        while (k > 0 && n + 1 < sizeof(b)) b[n++] = c[--k];
        b[n] = '\0';
        return *this;
    }
    Tekst& hex(uint64_t v) {
        static constexpr char cyfry[] = "0123456789ABCDEF";
        dodaj("0x");
        int start = 15;
        while (start > 0 && ((v >> (start * 4)) & 0xF) == 0) --start;
        for (int i = start; i >= 0 && n + 1 < sizeof(b); --i) b[n++] = cyfry[(v >> (i * 4)) & 0xF];
        b[n] = '\0';
        return *this;
    }
};

void wyczysc_ekran(Konsola& k, LogSzeregowy& log) {
    if (!k.wy->ClearScreen) return;
    const EFI_STATUS s = k.wy->ClearScreen(k.wy);
    if (EFI_ERROR(s)) log_status(log, "[BRB] konsola: ClearScreen nieudane (kontynuuje)", s);
}

EFI_STATUS ekran_naglowek(Konsola& k, LogSzeregowy& log) {
    wyczysc_ekran(k, log);
    ustaw_kolor(k, log, EFI_TEXT_ATTR(EFI_YELLOW, EFI_BLACK));
    EFI_STATUS s = wypisz(k, "Bursztynowy Program Rozruchowy");
    if (EFI_ERROR(s)) return s;
    ustaw_kolor(k, log, EFI_TEXT_ATTR(EFI_LIGHTGRAY, EFI_BLACK));
    const char* const linie[] = {"BRB wersja rozwojowa " BRB_WERSJA_TEKST, "Architektura x86-64", "Tryb rozruchu UEFI", ""};
    for (const char* linia : linie) {
        s = wypisz(k, linia);
        if (EFI_ERROR(s)) return s;
    }
    return EFI_SUCCESS;
}

EFI_STATUS ekran_stopka(Konsola& k, const Ustawienia& u) {
    const char* const linie[] = {
        "Inicjalizacja programu rozruchowego zakończona.",
        "Naciśnij dowolny klawisz, aby powrócić do oprogramowania układowego.",
    };
    for (const char* linia : linie) {
        const EFI_STATUS s = wypisz(k, linia);
        if (EFI_ERROR(s)) return s;
    }
    Tekst limit;
    limit.dodaj("(Automatyczny powrót po ").dziesietnie(u.limit_czasu_s).dodaj(" s bez naciśnięcia klawisza.)");
    return wypisz(k, limit.b);
}

void log_kod(LogSzeregowy& log, char grupa, uint32_t numer) {
    log.tekst("BRB-E");
    log.znak(grupa);
    log.znak(char('0' + (numer / 10) % 10));
    log.znak(char('0' + numer % 10));
}

void log_plan(LogSzeregowy& log, const PlanLadowania& p) {
    for (uint32_t i = 0; i < p.liczba_segmentow; ++i) {
        const SegmentPlanu& s = p.segmenty[i];
        log.tekst("[BRB] plan: seg");
        log.dziesietnie(i);
        log.tekst(" ph=");
        log.dziesietnie(s.indeks_ph);
        log.tekst(" plik=");
        log.hex(s.przesuniecie_pliku);
        log.tekst(" fiz=");
        log.hex(s.adres_fizyczny);
        log.tekst(" wirt=");
        log.hex(s.adres_wirtualny);
        log.tekst(" z_pliku=");
        log.hex(s.bajty_z_pliku);
        log.tekst(" pamiec=");
        log.hex(s.rozmiar_w_pamieci);
        log.tekst(" zera=");
        log.hex(s.bajty_zerowane);
        log.tekst(" wyr=");
        log.hex(s.wyrownanie);
        log.tekst(" prawa=");
        log.znak((s.prawa & ELF_PF_R) ? 'R' : '-');
        log.znak((s.prawa & ELF_PF_W) ? 'W' : '-');
        log.znak((s.prawa & ELF_PF_X) ? 'X' : '-');
        log.tekst(" strony=");
        log.hex(s.strona_poczatek);
        log.znak('-');
        log.hex(s.strona_koniec);
        log.tekst(" (");
        log.dziesietnie(s.liczba_stron);
        log.linia(")");
    }
    log.tekst("[BRB] plan: zakres_fiz=");
    log.hex(p.zakres_poczatek);
    log.znak('-');
    log.hex(p.zakres_koniec);
    log.tekst(" pamiec_stron=");
    log.dziesietnie(p.pamiec_stron);
    log.tekst(" B z_pliku=");
    log.dziesietnie(p.bajty_z_pliku);
    log.tekst(" B zera=");
    log.dziesietnie(p.bajty_zerowane);
    log.tekst(" B wejscie_wirt=");
    log.hex(p.wejscie_wirtualne);
    log.tekst(" wejscie_fiz=");
    log.hex(p.wejscie_fizyczne);
    log.tekst(" seg_wejscia=");
    log.dziesietnie(p.segment_wejscia);
    log.znak('\n');
    if (p.mb2.obecny) {
        log.tekst("[BRB] plan: multiboot2 plik=");
        log.hex(p.mb2.przesuniecie_pliku);
        log.tekst(" dlugosc=");
        log.dziesietnie(p.mb2.dlugosc);
        log.tekst(" architektura=");
        log.dziesietnie(p.mb2.architektura);
        log.tekst(" tagi=");
        log.dziesietnie(p.mb2.liczba_tagow);
        log.tekst(" segment=");
        if (p.mb2.segment >= 0) log.dziesietnie(uint64_t(p.mb2.segment)); else log.tekst("brak");
        log.znak('\n');
    }
}

// Fazy 3 i 4A: odczyt i walidacja jadra, plan ladowania, przydzial stron, ladowanie i weryfikacja obrazu.
// Zwraca blad KONSOLI (przerywa program); wynik dotyczacy jadra trafia do stan.status_jadra.
// Wlasnosc bufora pliku: zweryfikowane bajty zyja do konca ladowania i weryfikacji Fazy 4A (plik NIE jest
// czytany ponownie), potem bufor jest zwalniany na kazdej sciezce.
EFI_STATUS sprawdz_jadro(EFI_BOOT_SERVICES* bs, const EFI_LOADED_IMAGE_PROTOCOL* zaladowany, const DostepFizyczny& pamiec,
                         Konsola& k, LogSzeregowy& log, StanProgramu& stan) {
    BuforJadra bufor{0, 0, 0};
    const WynikPliku wp = wczytaj_plik_jadra(bs, zaladowany->DeviceHandle, SCIEZKA_JADRA, ELF_MAKS_ROZMIAR_PLIKU, &bufor, log);
    stan.blad_pliku = wp.blad;
    Tekst linia;
    if (wp.blad != BladPliku::Brak) {
        stan.status_jadra = wp.status;
        log.tekst("[BRB] blad pliku: ");
        log_kod(log, '2', uint32_t(wp.blad));
        log.tekst(" ");
        log.tekst(opis_bledu_pliku(wp.blad));
        log_status(log, "", wp.status);
        linia.dodaj("Błąd pliku jądra (BRB-E2").dziesietnie(uint32_t(wp.blad) / 10).dziesietnie(uint32_t(wp.blad) % 10)
            .dodaj("): ").dodaj(opis_bledu_pliku(wp.blad)).dodaj(".");
        return wypisz(k, linia.b);
    }
    stan.rozmiar_jadra = bufor.rozmiar;
    log.tekst("[BRB] plik: odczytano ");
    log.dziesietnie(bufor.rozmiar);
    log.tekst(" B, wywolania Read=");
    log.dziesietnie(wp.odczyty);
    log.tekst(", bufor fiz=");
    log.hex(bufor.adres);
    log.tekst(" strony=");
    log.dziesietnie(bufor.strony);
    log.linia(" (EfiLoaderData, tymczasowy)");

    PlanLadowania plan;
    const WynikElf we = zbuduj_plan_ladowania(bufor.dane(), bufor.rozmiar, POLITYKA_JADRA_BURSZTYN, &plan);
    stan.blad_elf = we.blad;
    auto zwolnij_bufor = [&]() {
        zwolnij_bufor_jadra(bs, &bufor);
        stan.bufor_zwolniony = true;
        log.linia("[BRB] plik: bufor tymczasowy zwolniony");
    };
    linia.dodaj("Jądro: ").dodaj(SCIEZKA_JADRA_ASCII).dodaj(" (").dziesietnie(stan.rozmiar_jadra).dodaj(" B)");
    EFI_STATUS s = wypisz(k, linia.b);
    if (EFI_ERROR(s)) { zwolnij_bufor(); return s; }
    Tekst wynik_linia;
    if (we.blad != BladElf::Brak) {
        zwolnij_bufor();
        stan.status_jadra = EFI_LOAD_ERROR;
        log.tekst("[BRB] blad ELF: ");
        log_kod(log, '1', uint32_t(we.blad));
        log.tekst(" ");
        log.tekst(opis_bledu_elf(we.blad));
        if (we.indeks_ph >= 0) {
            log.tekst(" ph=");
            log.dziesietnie(uint64_t(we.indeks_ph));
        }
        log.znak('\n');
        wynik_linia.dodaj("Błąd weryfikacji ELF64 (BRB-E1").dziesietnie(uint32_t(we.blad) / 10)
            .dziesietnie(uint32_t(we.blad) % 10).dodaj("): ").dodaj(opis_bledu_elf(we.blad)).dodaj(".");
        return wypisz(k, wynik_linia.b);
    }
    stan.segmenty = plan.liczba_segmentow;
    log.tekst("[BRB] etap 3: ELF64 poprawny (ET_EXEC, x86-64), PT_LOAD=");
    log.dziesietnie(plan.liczba_segmentow);
    log.tekst(" wejscie=");
    log.hex(plan.wejscie_wirtualne);
    log.znak('\n');
    log_plan(log, plan);
    log.linia("[BRB] etap 3: plan ladowania gotowy (zweryfikowany opis ladowania)");
    wynik_linia.dodaj("Weryfikacja ELF64: poprawna, segmenty: ").dziesietnie(plan.liczba_segmentow)
        .dodaj(", wejście: ").hex(plan.wejscie_wirtualne);
    s = wypisz(k, wynik_linia.b);
    if (EFI_ERROR(s)) { zwolnij_bufor(); return s; }
    Tekst plan_linia;
    plan_linia.dodaj("Plan ładowania: ").hex(plan.zakres_poczatek).dodaj("-").hex(plan.zakres_koniec).dodaj(" (")
        .dziesietnie(plan.pamiec_stron / 1024).dodaj(" KiB).");
    s = wypisz(k, plan_linia.b);
    if (EFI_ERROR(s)) { zwolnij_bufor(); return s; }

    // ---- Faza R2: metadane natywnego wejscia na zwalidowanym planie (bez drugiego parsera ELF) ----
    MetadaneWejscia meta;
    brb_memset(&meta, 0, sizeof(meta));
    const BladMetadanych bm = znajdz_metadane_wejscia(bufor.dane(), bufor.rozmiar, plan, &meta);
    stan.blad_metadanych = bm;
    if (bm == BladMetadanych::Brak) {
        log.tekst("[BRB] metadane: BRBJADRO plik=");
        log.hex(meta.przesuniecie_pliku);
        log.tekst(" segment=");
        log.dziesietnie(meta.segment);
        log.tekst(" kontrakt=");
        log.dziesietnie(meta.kontrakt_wejscia);
        log.tekst(" baza_wirt=");
        log.hex(meta.baza_wirtualna);
        log.tekst(" wejscie64=");
        log.hex(meta.wejscie64);
        log.tekst(" (segment ");
        log.dziesietnie(meta.segment_wejscia);
        log.tekst(") wyrownanie=");
        log.hex(meta.wyrownanie_podkladu);
        log.tekst(" cechy=");
        log.hex(meta.wymagane_cechy);
        log.znak('\n');
        log.tekst("[BRB] metadane: e_entry (wejscie MB2) ");
        log.hex(plan.wejscie_wirtualne);
        log.linia(" NIE jest uzywane przez sciezke natywna");
        RaportR2 r2;
        const CechyProcesora cpu = odczytaj_cechy_procesora();
        const EFI_STATUS s2 = faza_r2_zaladuj_natywnie(bs, zaladowany, bufor, plan, meta, cpu, pamiec, log, &r2);
        zwolnij_bufor();
        stan.status_r2 = s2;
        stan.blad_r2 = r2.blad;
        stan.etap_r2 = r2.etap;
        stan.baza_r2 = r2.umiejscowienie.baza;
        stan.bledy_sprzatania_r2 = r2.bledy_sprzatania;
        stan.status_jadra = s2;
        Tekst l2;
        if (r2.blad != BladR2::Brak) {
            l2.dodaj("Błąd fazy R2 (BRB-E6").dziesietnie(uint32_t(r2.blad) / 10).dziesietnie(uint32_t(r2.blad) % 10)
                .dodaj("): ").dodaj(opis_bledu_r2(r2.blad)).dodaj(".");
        } else {
            l2.dodaj("Faza R2: jądro załadowane pod adres fizyczny ").hex(r2.umiejscowienie.baza)
                .dodaj(", zweryfikowane, tablice przejścia sprawdzone; pamięć zwolniona.");
        }
        s = wypisz(k, l2.b);
        if (EFI_ERROR(s)) return s;
        return wypisz(k, "Jądro nie zostało uruchomione.");
    }
    log.tekst("[BRB] metadane: ");
    log_kod(log, '7', uint32_t(bm));
    log.tekst(" ");
    log.linia(opis_bledu_metadanych(bm));
#ifdef BRB_DIAG_STARY_4A
    if (bm != BladMetadanych::NieZnaleziono) {
#endif
        // Jadro bez poprawnych metadanych natywnych: brak przydzialu i brak jakiejkolwiek proby adresu stalego.
        zwolnij_bufor();
        stan.status_jadra = EFI_LOAD_ERROR;
        Tekst lm;
        lm.dodaj("Błąd metadanych wejścia natywnego (BRB-E7").dziesietnie(uint32_t(bm) / 10).dziesietnie(uint32_t(bm) % 10)
            .dodaj("): ").dodaj(opis_bledu_metadanych(bm)).dodaj(". Brak próby ładowania pod stały adres.");
        s = wypisz(k, lm.b);
        if (EFI_ERROR(s)) return s;
        return wypisz(k, "Jądro nie zostało uruchomione.");
#ifdef BRB_DIAG_STARY_4A
    }
    log.linia("[BRB] TRYB DIAGNOSTYCZNY 4A (wariant brb-x64-diag-4a.efi): jadro bez metadanych - historyczna sciezka adresu stalego");
#endif

    // ---- Faza 4A (historyczna sciezka diagnostyczna adresu stalego; tylko BRB_DIAG_STARY_4A) ----
#ifdef BRB_TEST_KONFLIKT_STRONA
    BlokadaTestowa blokada{};
    zajmij_strone_testowa(bs, BRB_TEST_KONFLIKT_STRONA, pamiec, &blokada, log);
#endif
    RaportFazy4A r4;
    const EFI_STATUS s4 = faza4a_zaladuj_i_zweryfikuj(bs, zaladowany, bufor, plan, pamiec, log, &r4);
#ifdef BRB_TEST_KONFLIKT_STRONA
    zwolnij_strone_testowa(bs, pamiec, &blokada, log);
#endif
    zwolnij_bufor();
    stan.status_4a = s4;
    stan.blad_4a = r4.blad;
    stan.etap_4a = r4.etap;
    stan.strony_4a = r4.zakresy.strony_lacznie;
    stan.bledy_sprzatania_4a = r4.bledy_sprzatania;
    stan.status_jadra = s4;
    Tekst l4;
    if (r4.blad != BladPrzydzialu::Brak) {
        l4.dodaj("Błąd fazy 4A (BRB-E5").dziesietnie(uint32_t(r4.blad) / 10).dziesietnie(uint32_t(r4.blad) % 10)
            .dodaj("): ").dodaj(opis_bledu_przydzialu(r4.blad)).dodaj(".");
    } else {
        l4.dodaj("Faza 4A: segmenty załadowane i zweryfikowane (stron: ").dziesietnie(r4.zakresy.strony_lacznie)
            .dodaj("), pamięć zwolniona.");
    }
    s = wypisz(k, l4.b);
    if (EFI_ERROR(s)) return s;
    return wypisz(k, "Jądro nie zostało uruchomione.");
}

// Oproznienie bufora klawiatury przed oczekiwaniem (klawisze nacisniete wczesniej, np. w menu firmware).
EFI_STATUS oproznij_klawiature(EFI_SIMPLE_TEXT_INPUT_PROTOCOL* we) {
    for (UINT32 i = 0; i < MAKS_OPROZNIANIE_BUFORA; ++i) {
        EFI_INPUT_KEY klawisz{};
        const EFI_STATUS s = we->ReadKeyStroke(we, &klawisz);
        if (s == EFI_NOT_READY) return EFI_SUCCESS;
        if (EFI_ERROR(s)) return s;
    }
    return EFI_SUCCESS;  // ciagly strumien klawiszy - nie blokujemy sie, oczekiwanie i tak ma limit
}

// Tryb awaryjny bez zdarzen: Stall (oczekiwanie firmware, nie aktywna petla na CPU) + ReadKeyStroke,
// ograniczone do limitu czasu.
WynikKlawisza czekaj_awaryjnie(EFI_BOOT_SERVICES* bs, EFI_SIMPLE_TEXT_INPUT_PROTOCOL* we, const Ustawienia& u,
                               EFI_STATUS* status) {
    const UINT32 krok = u.krok_awaryjny_ms ? u.krok_awaryjny_ms : 100;
    const UINT64 kroki = (UINT64(u.limit_czasu_s) * 1000u + krok - 1) / krok;
    for (UINT64 i = 0; i < kroki; ++i) {
        EFI_INPUT_KEY klawisz{};
        const EFI_STATUS s = we->ReadKeyStroke(we, &klawisz);
        if (s == EFI_SUCCESS) return WynikKlawisza::Klawisz;
        if (s != EFI_NOT_READY) { *status = s; return WynikKlawisza::BladUrzadzenia; }
        bs->Stall(UINTN(krok) * 1000u);
    }
    return WynikKlawisza::LimitCzasu;
}

WynikKlawisza czekaj_na_klawisz(EFI_BOOT_SERVICES* bs, EFI_SIMPLE_TEXT_INPUT_PROTOCOL* we, const Ustawienia& u,
                                LogSzeregowy& log, StanProgramu& stan, EFI_STATUS* status) {
    *status = oproznij_klawiature(we);
    if (EFI_ERROR(*status)) return WynikKlawisza::BladUrzadzenia;

    EFI_EVENT zegar = nullptr;
    EFI_STATUS s = we->WaitForKey ? bs->CreateEvent(EVT_TIMER, TPL_CALLBACK, nullptr, nullptr, &zegar)
                                  : EFI_UNSUPPORTED;
    if (s == EFI_SUCCESS && !zegar) s = EFI_DEVICE_ERROR;
    if (s == EFI_SUCCESS) {
        // TriggerTime w jednostkach 100 ns (7.1 SetTimer).
        s = bs->SetTimer(zegar, TimerRelative, UINT64(u.limit_czasu_s) * 10000000ull);
    }
    if (EFI_ERROR(s)) {
        log_status(log, "[BRB] klawiatura: zdarzenia niedostepne - tryb awaryjny Stall", s);
        if (zegar) bs->CloseEvent(zegar);
        stan.tryb_awaryjny_klawiatury = true;
        *status = EFI_SUCCESS;
        return czekaj_awaryjnie(bs, we, u, status);
    }
    log.tekst("[BRB] klawiatura: oczekiwanie (WaitForEvent, limit ");
    log.dziesietnie(u.limit_czasu_s);
    log.linia(" s)");

    WynikKlawisza wynik = WynikKlawisza::LimitCzasu;
    EFI_EVENT zdarzenia[2] = {we->WaitForKey, zegar};
    bool awaryjnie = false;
    for (UINT32 proba = 0; proba < MAKS_POBUDZEN_BEZ_KLAWISZA; ++proba) {
        UINTN indeks = ~UINTN(0);
        s = bs->WaitForEvent(2, zdarzenia, &indeks);
        if (EFI_ERROR(s) || indeks > 1) {
            log_status(log, "[BRB] klawiatura: WaitForEvent nieudane - tryb awaryjny Stall", s);
            awaryjnie = true;
            break;
        }
        if (indeks == 1) { wynik = WynikKlawisza::LimitCzasu; break; }
        EFI_INPUT_KEY klawisz{};
        s = we->ReadKeyStroke(we, &klawisz);
        if (s == EFI_SUCCESS) { wynik = WynikKlawisza::Klawisz; break; }
        if (s != EFI_NOT_READY) { *status = s; wynik = WynikKlawisza::BladUrzadzenia; break; }
        // Pobudzenie bez klawisza (dopuszczalne) - kolejna proba, liczba prob ograniczona.
    }
    bs->SetTimer(zegar, TimerCancel, 0);
    bs->CloseEvent(zegar);
    if (awaryjnie) {
        stan.tryb_awaryjny_klawiatury = true;
        *status = EFI_SUCCESS;
        return czekaj_awaryjnie(bs, we, u, status);
    }
    return wynik;
}

EFI_STATUS blad_kontrolowany(Konsola* k, LogSzeregowy& log, const char* opis_pl, const char* opis_log,
                             EFI_STATUS s) {
    log.tekst("[BRB] blad kontrolowany: ");
    log_status(log, opis_log, s);
    if (k) {
        ustaw_kolor(*k, log, EFI_TEXT_ATTR(EFI_YELLOW, EFI_BLACK));
        (void)wypisz(*k, opis_pl);
        ustaw_kolor(*k, log, EFI_TEXT_ATTR(EFI_LIGHTGRAY, EFI_BLACK));
    }
    log_status(log, "[BRB] zakonczenie: powrot do firmware", s);
    return s;
}

}  // namespace

const char* opis_bledu_firmware(BladFirmware blad) {
    switch (blad) {
        case BladFirmware::Brak: return "brak";
        case BladFirmware::BrakUchwytuObrazu: return "brak uchwytu obrazu";
        case BladFirmware::BrakTablicySystemowej: return "brak tablicy systemowej";
        case BladFirmware::ZlaSygnaturaTablicy: return "zla sygnatura tablicy systemowej";
        case BladFirmware::ZaMalaTablica: return "za maly rozmiar tablicy systemowej";
        case BladFirmware::ZaStaraRewizja: return "rewizja UEFI ponizej 2.0";
        case BladFirmware::BrakUslugRozruchowych: return "brak uslug rozruchowych";
        case BladFirmware::ZlaSygnaturaUslug: return "zla sygnatura uslug rozruchowych";
        case BladFirmware::ZaMaleUslugi: return "za maly rozmiar tablicy uslug rozruchowych";
        case BladFirmware::BrakFunkcjiUslug: return "brak wymaganej funkcji uslug rozruchowych";
        case BladFirmware::BrakKonsoliWyjscia: return "brak konsoli tekstowej (ConOut)";
    }
    return "nieznany";
}

const char* nazwa_statusu(EFI_STATUS s) {
    switch (s) {
        case EFI_SUCCESS: return "EFI_SUCCESS";
        case EFI_WARN_UNKNOWN_GLYPH: return "EFI_WARN_UNKNOWN_GLYPH";
        case EFI_LOAD_ERROR: return "EFI_LOAD_ERROR";
        case EFI_INVALID_PARAMETER: return "EFI_INVALID_PARAMETER";
        case EFI_UNSUPPORTED: return "EFI_UNSUPPORTED";
        case EFI_BAD_BUFFER_SIZE: return "EFI_BAD_BUFFER_SIZE";
        case EFI_BUFFER_TOO_SMALL: return "EFI_BUFFER_TOO_SMALL";
        case EFI_NOT_READY: return "EFI_NOT_READY";
        case EFI_DEVICE_ERROR: return "EFI_DEVICE_ERROR";
        case EFI_OUT_OF_RESOURCES: return "EFI_OUT_OF_RESOURCES";
        case EFI_NOT_FOUND: return "EFI_NOT_FOUND";
        case EFI_TIMEOUT: return "EFI_TIMEOUT";
        case EFI_ABORTED: return "EFI_ABORTED";
        case EFI_INCOMPATIBLE_VERSION: return "EFI_INCOMPATIBLE_VERSION";
        default: return EFI_ERROR(s) ? "inny blad" : "inne ostrzezenie";
    }
}

WynikWalidacji waliduj_firmware(EFI_HANDLE obraz, const EFI_SYSTEM_TABLE* st) {
    if (!obraz) return {EFI_INVALID_PARAMETER, BladFirmware::BrakUchwytuObrazu};
    if (!st) return {EFI_INVALID_PARAMETER, BladFirmware::BrakTablicySystemowej};
    // Najpierw naglowek (24 B), dopiero potem pola zalezne od HeaderSize.
    if (st->Hdr.Signature != EFI_SYSTEM_TABLE_SIGNATURE)
        return {EFI_INCOMPATIBLE_VERSION, BladFirmware::ZlaSygnaturaTablicy};
    if (st->Hdr.HeaderSize < sizeof(EFI_SYSTEM_TABLE))
        return {EFI_INCOMPATIBLE_VERSION, BladFirmware::ZaMalaTablica};
    if (st->Hdr.Revision < MIN_REWIZJA_UEFI)
        return {EFI_INCOMPATIBLE_VERSION, BladFirmware::ZaStaraRewizja};
    if (!st->ConOut || !st->ConOut->OutputString)
        return {EFI_UNSUPPORTED, BladFirmware::BrakKonsoliWyjscia};
    const EFI_BOOT_SERVICES* bs = st->BootServices;
    if (!bs) return {EFI_UNSUPPORTED, BladFirmware::BrakUslugRozruchowych};
    if (bs->Hdr.Signature != EFI_BOOT_SERVICES_SIGNATURE)
        return {EFI_INCOMPATIBLE_VERSION, BladFirmware::ZlaSygnaturaUslug};
    if (bs->Hdr.HeaderSize < MIN_ROZMIAR_USLUG)
        return {EFI_INCOMPATIBLE_VERSION, BladFirmware::ZaMaleUslugi};
    if (!bs->CreateEvent || !bs->SetTimer || !bs->WaitForEvent || !bs->CloseEvent || !bs->HandleProtocol ||
        !bs->Stall)
        return {EFI_UNSUPPORTED, BladFirmware::BrakFunkcjiUslug};
    return {EFI_SUCCESS, BladFirmware::Brak};
}

bool sprawdz_obraz(const EFI_LOADED_IMAGE_PROTOCOL* obraz, const EFI_SYSTEM_TABLE* st) {
    if (!obraz || obraz->Revision < EFI_LOADED_IMAGE_PROTOCOL_REVISION || obraz->SystemTable != st) return false;
    const uintptr_t baza = reinterpret_cast<uintptr_t>(obraz->ImageBase);
    const UINT64 rozmiar = obraz->ImageSize;
    if (baza == 0 || rozmiar == 0 || rozmiar > UINT64(~uintptr_t(0)) - baza) return false;
    const uintptr_t kod = reinterpret_cast<uintptr_t>(&sprawdz_obraz);
    const uintptr_t dane = reinterpret_cast<uintptr_t>(ZNACZNIK_RELOKACJI);
    if (kod < baza || kod - baza >= rozmiar) return false;
    if (dane < baza || dane - baza >= rozmiar) return false;
    return reinterpret_cast<uintptr_t>(WSKAZNIK_RELOKACJI) == dane;
}

EFI_STATUS uruchom(EFI_HANDLE obraz, EFI_SYSTEM_TABLE* st, LogSzeregowy& log, const Ustawienia& u,
                   const DostepFizyczny& pamiec, StanProgramu* stan_wy) {
    StanProgramu stan_lokalny;
    StanProgramu& stan = stan_wy ? *stan_wy : stan_lokalny;
    log.linia("[BRB] etap 1: wejscie UEFI");
    log.linia("[BRB] wersja " BRB_WERSJA_TEKST " (rozwojowa), architektura x86-64, tryb UEFI");

    const WynikWalidacji wal = waliduj_firmware(obraz, st);
    if (EFI_ERROR(wal.status)) {
        log.tekst("[BRB] kontrola firmware: BLAD - ");
        log.linia(opis_bledu_firmware(wal.blad));
        // ConOut jest sprawdzane zaraz po naglowku tablicy systemowej, wiec konsoli wolno uzyc tylko przy bledach
        // wykrytych pozniej (w tablicy uslug rozruchowych). Tekst bez polskich znakow (TestString niesprawdzone).
        Konsola k{st ? st->ConOut : nullptr, true, 0};
        const bool konsola_ok = wal.blad == BladFirmware::BrakUslugRozruchowych ||
                                wal.blad == BladFirmware::ZlaSygnaturaUslug ||
                                wal.blad == BladFirmware::ZaMaleUslugi || wal.blad == BladFirmware::BrakFunkcjiUslug;
        return blad_kontrolowany(konsola_ok ? &k : nullptr, log,
                                 "Błąd: oprogramowanie układowe UEFI nie spełnia wymagań programu rozruchowego.",
                                 "kontrola firmware", wal.status);
    }
    log.tekst("[BRB] kontrola firmware: OK rewizja=");
    log.dziesietnie(st->Hdr.Revision >> 16);
    log.znak('.');
    log.dziesietnie(st->Hdr.Revision & 0xFFFFu);
    log.tekst(" tablica=");
    log.dziesietnie(st->Hdr.HeaderSize);
    log.tekst(" uslugi=");
    log.dziesietnie(st->BootServices->Hdr.HeaderSize);
    log.linia(" B");

    EFI_BOOT_SERVICES* bs = st->BootServices;
    Konsola k{st->ConOut, false, 0};

    EFI_LOADED_IMAGE_PROTOCOL* zaladowany = nullptr;
    EFI_STATUS s = bs->HandleProtocol(obraz, &GUID_LOADED_IMAGE, reinterpret_cast<VOID**>(&zaladowany));
    if (EFI_ERROR(s) || !zaladowany) {
        k.transliteracja = true;
        return blad_kontrolowany(&k, log, "Błąd: brak protokołu EFI_LOADED_IMAGE dla programu rozruchowego.",
                                 "LoadedImage", EFI_ERROR(s) ? s : EFI_NOT_FOUND);
    }
    stan.relokacja_ok = sprawdz_obraz(zaladowany, st);
    log.tekst("[BRB] obraz: baza=");
    log.hex(reinterpret_cast<uintptr_t>(zaladowany->ImageBase));
    log.tekst(" rozmiar=");
    log.dziesietnie(zaladowany->ImageSize);
    log.linia(stan.relokacja_ok ? " relokacja=OK" : " relokacja=BLAD");
    if (!stan.relokacja_ok) {
        k.transliteracja = true;
        return blad_kontrolowany(&k, log, "Błąd: obraz programu rozruchowego został załadowany niepoprawnie.",
                                 "kontrola obrazu/relokacji", EFI_LOAD_ERROR);
    }

    k.transliteracja = potrzebna_transliteracja(k.wy, log, u);
    stan.transliteracja = k.transliteracja;
    s = ekran_naglowek(k, log);
    if (EFI_ERROR(s)) return blad_kontrolowany(nullptr, log, "", "konsola: OutputString", s);
    log.tekst("[BRB] konsola: gotowa, polskie znaki=");
    log.linia(k.transliteracja ? "transliteracja ASCII" : "tak");

    s = sprawdz_jadro(bs, zaladowany, pamiec, k, log, stan);
    if (EFI_ERROR(s)) return blad_kontrolowany(nullptr, log, "", "konsola: OutputString", s);
    s = ekran_stopka(k, u);
    if (EFI_ERROR(s)) return blad_kontrolowany(nullptr, log, "", "konsola: OutputString", s);
    stan.ostrzezenia_glifow = k.ostrzezenia;

    EFI_SIMPLE_TEXT_INPUT_PROTOCOL* we = st->ConIn;
    if (!we || !we->ReadKeyStroke) {
        log.linia("[BRB] klawiatura: brak ConIn - powrot po krotkim oczekiwaniu");
        (void)wypisz(k, "Klawiatura UEFI jest niedostępna. Powrót do oprogramowania układowego.");
        for (UINT32 i = 0; i < u.czekanie_bez_klawiatury_s * 10u; ++i) bs->Stall(100000);
        const EFI_STATUS st_bez = EFI_ERROR(stan.status_jadra) ? stan.status_jadra : EFI_UNSUPPORTED;
        log_status(log, "[BRB] zakonczenie: powrot do firmware", st_bez);
        return st_bez;
    }

    // Status powrotu: blad jadra (Faza 3) ma pierwszenstwo przed wynikiem oczekiwania na klawisz.
    EFI_STATUS status_klawiatury = EFI_SUCCESS;
    stan.klawisz = czekaj_na_klawisz(bs, we, u, log, stan, &status_klawiatury);
    switch (stan.klawisz) {
        case WynikKlawisza::Klawisz: {
            log.linia("[BRB] klawiatura: klawisz odebrany");
            (void)wypisz(k, "Powrót do oprogramowania układowego.");
            const EFI_STATUS st_k = EFI_ERROR(stan.status_jadra) ? stan.status_jadra : EFI_SUCCESS;
            log_status(log, "[BRB] zakonczenie: powrot do firmware", st_k);
            return st_k;
        }
        case WynikKlawisza::LimitCzasu: {
            log.linia("[BRB] klawiatura: limit czasu bez klawisza");
            (void)wypisz(k, "Upłynął limit czasu. Powrót do oprogramowania układowego.");
            const EFI_STATUS st_l = EFI_ERROR(stan.status_jadra) ? stan.status_jadra : EFI_TIMEOUT;
            log_status(log, "[BRB] zakonczenie: powrot do firmware", st_l);
            return st_l;
        }
        case WynikKlawisza::BladUrzadzenia:
            break;
    }
    return blad_kontrolowany(&k, log, "Błąd klawiatury UEFI. Powrót do oprogramowania układowego.",
                             "klawiatura: ReadKeyStroke", status_klawiatury);
}

}  // namespace brb
