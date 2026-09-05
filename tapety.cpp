#include "tapety.h"
#include "grafika.h"
#include "heap.h"
#include "psf.h"
#include "narzedzia/tapety-btp/tapetybtp.h"
#include "sterowniki/czas/hpet.h"

extern "C" {
#define BTPZASOB(n) extern uint8_t _binary_obrazy_tapety_bursztyn##n##_btp_start[]; \
                    extern uint8_t _binary_obrazy_tapety_bursztyn##n##_btp_end[];
BTPZASOB(01) BTPZASOB(02) BTPZASOB(03) BTPZASOB(04) BTPZASOB(05)
BTPZASOB(06) BTPZASOB(07) BTPZASOB(08) BTPZASOB(09) BTPZASOB(10)
#undef BTPZASOB
}

namespace {
struct Zasob { const uint8_t* poczatek; const uint8_t* koniec; };
#define BTPZASOB(n) {_binary_obrazy_tapety_bursztyn##n##_btp_start, _binary_obrazy_tapety_bursztyn##n##_btp_end}
const Zasob zasoby[TAPETY_LICZBA] = {
    BTPZASOB(01), BTPZASOB(02), BTPZASOB(03), BTPZASOB(04), BTPZASOB(05),
    BTPZASOB(06), BTPZASOB(07), BTPZASOB(08), BTPZASOB(09), BTPZASOB(10)
};
#undef BTPZASOB

TapetyKonfiguracja aktualna{};
TapetyOdpowiedz statystyki{};
bool bufory_gotowe = false;

void log_sciezka(const char* opis, const char* sciezka) {
    char buf[224]{};
    size_t p = 0;
    for (size_t i = 0; opis[i] && p + 1 < sizeof(buf); ++i) buf[p++] = opis[i];
    for (size_t i = 0; sciezka[i] && p + 1 < sizeof(buf); ++i) buf[p++] = sciezka[i];
    klog_serial(buf);
}
void log_liczba(const char* opis, uint64_t n) {
    char buf[128]{}, cyfry[24]; size_t p = 0, k = 0;
    for (; opis[p] && p + 1 < sizeof(buf); ++p) buf[p] = opis[p];
    do { cyfry[k++] = static_cast<char>('0' + n % 10); n /= 10; } while (n && k < sizeof(cyfry));
    while (k && p + 1 < sizeof(buf)) buf[p++] = cyfry[--k];
    klog_serial(buf);
}
bool czytaj_dokladnie(const char* path, void* buf, uint32_t rozmiar) {
    uint32_t odczytano = 0;
    return czytaj_fragment_pliku(path, 0, static_cast<char*>(buf), rozmiar, &odczytano) &&
           odczytano == rozmiar && rozmiar_pliku(path) == rozmiar;
}

void instaluj() {
    if (!psf_czy_gotowy() || !zapewnij_katalog("/tapety")) {
        klog_serial("[TAPETA] BLAD utworzenia /tapety; start bedzie kontynuowany.");
        return;
    }
    uint32_t nowe = 0, zachowane = 0;
    for (uint32_t i = 0; i < TAPETY_LICZBA; ++i) {
        psf_metadane meta{};
        if (pobierz_metadane_tworu(TAPETY_SCIEZKI[i], &meta)) {
            ++zachowane; // Takze zmodyfikowany/uszkodzony plik: nie nadpisujemy.
            continue;
        }
        const uintptr_t a = reinterpret_cast<uintptr_t>(zasoby[i].poczatek);
        const uintptr_t b = reinterpret_cast<uintptr_t>(zasoby[i].koniec);
        BtpInfo info{};
        if (b < a || b - a > TAPETY_PLIK_MAX ||
            btpsprawdzplik(zasoby[i].poczatek, b - a, &info) != BTPOK) {
            log_sciezka("[TAPETA] BLAD zasobu instalacyjnego ", TAPETY_SCIEZKI[i]);
            continue;
        }
        if (!utworz_plik(TAPETY_SCIEZKI[i])) {
            log_sciezka("[TAPETA] BLAD tworzenia pliku ", TAPETY_SCIEZKI[i]);
            continue;
        }
        if (!zapisz_do_pliku(TAPETY_SCIEZKI[i], reinterpret_cast<const char*>(zasoby[i].poczatek),
                             static_cast<uint32_t>(b - a)) || !psf_flush_cache()) {
            log_sciezka("[TAPETA] BLAD zapisu (I/O lub brak miejsca) ", TAPETY_SCIEZKI[i]);
            (void)usun_twor(TAPETY_SCIEZKI[i]); // Tylko plik utworzony w tej probie.
            continue;
        }
        ++nowe;
    }
    log_liczba("[TAPETA] Zainstalowano brakujacych domyslnych plikow: ", nowe);
    log_liczba("[TAPETA] Zachowano istniejacych domyslnych plikow: ", zachowane);
}

bool wczytaj(const char* path, BtpInfo* info, uint32_t** zrodlo, bool cicho) {
    if (!bufory_gotowe || !info || !zrodlo || !tapety_sciezka(path)) return false;
    const uint32_t rozmiar = rozmiar_pliku(path);
    size_t cap = 0;
    uint8_t* scratch = grafika_scratch_tapety(&cap);
    if (!scratch || rozmiar < sizeof(BtpNaglowek) || rozmiar > TAPETY_PLIK_MAX || rozmiar > cap) {
        if (!cicho) log_sciezka("[TAPETA] BLAD brak lub za duzy plik ", path);
        return false;
    }
    uint64_t t = czas_monotoniczny_us();
    ++statystyki.odczyty;
    if (!czytaj_dokladnie(path, scratch, rozmiar)) {
        if (!cicho) log_sciezka("[TAPETA] BLAD odczytu ", path);
        return false;
    }
    statystyki.odczyt_us = czas_monotoniczny_us() - t;
    t = czas_monotoniczny_us();
    if (btpnaglowek(scratch, rozmiar, info) != BTPOK) {
        if (!cicho) log_sciezka("[TAPETA] BLAD uszkodzony plik ", path);
        return false;
    }
    const uint64_t count = static_cast<uint64_t>(info->szerokosc) * info->wysokosc;
    const size_t offset = (static_cast<size_t>(rozmiar) + 4095U) & ~size_t(4095U);
    if (count > TAPETY_PIKSELE_MAX || offset > cap || count * 4 > cap - offset) return false;
    uint32_t* piksele = reinterpret_cast<uint32_t*>(scratch + offset);
    for (size_t i = 0; i < count; ++i) piksele[i] = TAPETY_TLO;
    ++statystyki.dekodowania;
    if (btpwczytajirysuj(scratch, rozmiar, piksele, info->szerokosc, info->wysokosc,
                         info->szerokosc, static_cast<size_t>(count * 4)) != BTPOK) return false;
    statystyki.dekoduj_us = czas_monotoniczny_us() - t;
    *zrodlo = piksele;
    return true;
}

bool zastosuj(const TapetyKonfiguracja& c, bool cicho) {
    if (static_cast<uint32_t>(c.tryb) > 2) return false;
    BtpInfo info{}; uint32_t* src = nullptr;
    if (!wczytaj(c.sciezka, &info, &src, cicho)) return false;
    size_t cap = 0;
    uint32_t* dst = grafika_bufor_tapety(&cap);
    const int wi = grafika_pobierz_szerokosc(), hi = grafika_pobierz_wysokosc();
    if (wi <= 0 || hi <= 0) return false;
    const uint32_t w = static_cast<uint32_t>(wi), h = static_cast<uint32_t>(hi);
    const uint64_t t = czas_monotoniczny_us();
    if (!skaluj_obraz(src, info.szerokosc, info.wysokosc,
        static_cast<size_t>(info.szerokosc) * info.wysokosc * 4, dst, w, h, w,
        cap, c.tryb, TAPETY_TLO)) return false;
    statystyki.skaluj_us = czas_monotoniczny_us() - t;
    ++statystyki.przygotowania;
    ++statystyki.zmiany;
    statystyki.zrodlo = TAPETY_BTP;
    aktualna = c;
    grafika_publikuj_tapete();
    if (!cicho) {
        log_sciezka("[TAPETA] Wybrano ", c.sciezka);
        log_liczba("[TAPETA] BTP szerokosc ", info.szerokosc);
        log_liczba("[TAPETA] BTP wysokosc ", info.wysokosc);
        klog_serial(info.kompresja ? "[TAPETA] RLE" : "[TAPETA] RAW");
        log_sciezka("[TAPETA] Tryb ", tapety_nazwa_trybu(c.tryb));
        log_liczba("[TAPETA] Odczyt us ", statystyki.odczyt_us);
        log_liczba("[TAPETA] Dekodowanie us ", statystyki.dekoduj_us);
        log_liczba("[TAPETA] Przygotowanie us ", statystyki.skaluj_us);
        klog_serial("[TAPETA] Tapeta gotowa.");
    }
    return true;
}

bool zapisz_wybor() {
    char buf[TAPETY_CFG_MAX]{};
    const size_t n = tapety_serializuj(aktualna, buf, sizeof(buf));
    if (!n || !psf_czy_gotowy() || !zapewnij_katalog("/ustawienia")) return false;
    psf_metadane meta{};
    if (!pobierz_metadane_tworu(TAPETY_CFG, &meta) && !utworz_plik(TAPETY_CFG)) return false;
    // BSP2 nie ma atomic replace ani journalu. Maly zapis + flush + readback.
    // Awaria zapisu moze uszkodzic config; nastepny boot wybierze domyslna.
    if (!zapisz_do_pliku(TAPETY_CFG, buf, static_cast<uint32_t>(n)) || !psf_flush_cache()) return false;
    char kontrola[TAPETY_CFG_MAX]{};
    if (!czytaj_dokladnie(TAPETY_CFG, kontrola, static_cast<uint32_t>(n))) return false;
    for (size_t i = 0; i < n; ++i) if (buf[i] != kontrola[i]) return false;
    return true;
}

void snapshot(TapetyOdpowiedz* out) {
    *out = statystyki;
    out->wersja = 1;
    out->tryb = static_cast<uint32_t>(aktualna.tryb);
    tapety_kopiuj(out->sciezka, aktualna.sciezka, sizeof(out->sciezka));
    size_t cap = 0;
    out->adres_bufora = reinterpret_cast<uint64_t>(grafika_bufor_tapety(&cap));
    out->bufor_bajty = cap;
    (void)grafika_scratch_tapety(&cap);
    out->scratch_bajty = cap;
    out->szerokosc = static_cast<uint32_t>(grafika_pobierz_szerokosc());
    out->wysokosc = static_cast<uint32_t>(grafika_pobierz_wysokosc());
}

void diagnostyka(uint32_t grupa, TapetyOdpowiedz* out) {
    uint32_t ok = 0, bledy = 0;
    uint64_t przed = 0, po = 0;
    if (grupa == 3) {
        if (!czy_katalog_istnieje("/tapety")) ++bledy;
        for (uint32_t i = 0; i < TAPETY_LICZBA; ++i) {
            BtpInfo info{}; uint32_t* src = nullptr;
            if (wczytaj(TAPETY_SCIEZKI[i], &info, &src, true)) ++ok;
            else ++bledy;
        }
    } else if (grupa == 6) {
        const TapetyKonfiguracja stara = aktualna;
        size_t cap = 0;
        const uint32_t* adres = grafika_bufor_tapety(&cap);
        const size_t stare_bajty = cap;
        przed = heap_zajete_bajty();
        for (uint32_t i = 0; i < 100; ++i) {
            TapetyKonfiguracja c = tapety_domyslna();
            tapety_kopiuj(c.sciezka, TAPETY_SCIEZKI[i % TAPETY_LICZBA], sizeof(c.sciezka));
            c.tryb = static_cast<TrybSkalowaniaObrazu>(i % 3);
            if (zastosuj(c, true) && grafika_bufor_tapety(&cap) == adres && cap == stare_bajty) ++ok;
            else ++bledy;
        }
        if (!zastosuj(stara, true)) ++bledy;
        po = heap_zajete_bajty();
        if (przed == UINT64_MAX || przed != po) ++bledy;
        log_liczba("[TAPETA TEST] Zmian z 100: ", ok);
        log_liczba("[TAPETA TEST] Heap przed: ", przed);
        log_liczba("[TAPETA TEST] Heap po: ", po);
    } else ++bledy;
    snapshot(out);
    out->poprawne = ok; out->bledy = bledy;
    out->heap_przed = przed; out->heap_po = po;
    out->status = bledy ? TAPETY_BLAD : TAPETY_OK;
}
} // namespace

void tapety_inicjalizuj() {
    aktualna = tapety_domyslna();
    instaluj();
    bufory_gotowe = grafika_przygotuj_bufory_tapety();
    if (!bufory_gotowe) klog_serial("[TAPETA] BLAD buforow BTP; uzywam fallback.");
    TapetyKonfiguracja wybor = tapety_domyslna();
    const uint32_t n = rozmiar_pliku(TAPETY_CFG);
    if (n) {
        char cfg[TAPETY_CFG_MAX]{};
        if (n > sizeof(cfg) || !czytaj_dokladnie(TAPETY_CFG, cfg, n) || !tapety_parsuj(cfg, n, &wybor))
            klog_serial("[TAPETA] BLAD konfiguracji; uzywam domyslnej tapety.");
    }
    statystyki.zrodlo = tapety_fallback(wybor,
        [](const TapetyKonfiguracja& c) { return zastosuj(c, false); },
        []() { klog_serial("[TAPETA] Fallback BMP LBA 10."); wczytaj_tapete_z_dysku(); return grafika_czy_tapeta(); },
        []() { klog_serial("[TAPETA] Fallback jednolity kolor."); grafika_tapeta_solid(); });
    if (statystyki.zrodlo != TAPETY_BTP) aktualna.sciezka[0] = 0;
    TapetyOdpowiedz s{}; snapshot(&s);
    log_liczba("[TAPETA] Bufor aktywny bajty ", s.bufor_bajty);
    log_liczba("[TAPETA] Scratch wspolny BMP/BTP bajty ", s.scratch_bajty);
    bws_ipc_inicjalizuj_tapety();
}

// Wywolywane przez zweryfikowany IPC endpoint, pod istniejaca blokada ekranu.
bool tapety_obsluz(const proces_t& proces, const TapetyZadanie& req, TapetyOdpowiedz* out) {
    if (!out || req.wersja != 1 || !pzb_ma_co_najmniej_poziom(&proces, PZB_UZYTKOWNIK) ||
        !pzb_ma_prawa(&proces, PRAWO_GUI)) return false;
    const bool app = tapety_rowne(proces.sciezka_pliku, TAPETY_PROGRAM, sizeof(proces.sciezka_pliku));
    const bool test = tapety_rowne(proces.sciezka_pliku, TAPETY_TEST_PROGRAM, sizeof(proces.sciezka_pliku));
    if ((!app && !test) || (app && !pzb_ma_prawa(&proces, PRAWO_PLIKI_CZYTAJ))) return false;
    snapshot(out);
    out->status = TAPETY_BLAD;
    if (req.operacja == TAPETY_STAN) { out->status = TAPETY_OK; return true; }
    if (req.operacja == TAPETY_DIAGNOSTYKA && test && (req.grupa == 3 || req.grupa == 6)) {
        diagnostyka(req.grupa, out);
        return true;
    }
    if (req.operacja != TAPETY_ZASTOSUJ || req.grupa != 0 || req.tryb > 2 || !tapety_sciezka(req.sciezka)) return false;
    TapetyKonfiguracja c{};
    tapety_kopiuj(c.sciezka, req.sciezka, sizeof(c.sciezka));
    c.tryb = static_cast<TrybSkalowaniaObrazu>(req.tryb);
    if (!zastosuj(c, false)) return true;
    const bool zapis = zapisz_wybor();
    snapshot(out);
    out->status = zapis ? TAPETY_OK : TAPETY_BLAD_ZAPISU;
    klog_serial(zapis ? "[TAPETA] Wybor zapisany w BSP2." : "[TAPETA] BLAD zapisu wyboru; tapeta aktywna tylko w tej sesji.");
    return true;
}
