#include "tapety.h"
#include "grafika.h"
#include "heap.h"
#include "psf.h"
#include "narzedzia/tapety-btp/tapetybtp.h"
#include "sterowniki/czas/hpet.h"

#include "pamiec.h"

extern uint64_t g_multiboot_info_ptr;

namespace {
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

uint32_t tapety_plik_crc32(const char* path, uint32_t rozmiar) {
    if (!path || !rozmiar) return 0;
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t off = 0;
    uint8_t bufor[1024];
    while (off < rozmiar) {
        uint32_t ile = rozmiar - off;
        if (ile > sizeof(bufor)) ile = sizeof(bufor);
        uint32_t got = 0;
        if (!czytaj_fragment_pliku(path, off, reinterpret_cast<char*>(bufor), ile, &got) || got != ile) {
            return 0;
        }
        for (uint32_t i = 0; i < got; ++i) {
            crc ^= bufor[i];
            for (int k = 0; k < 8; ++k) {
                crc = (crc >> 1) ^ (0xEDB88320U & -(crc & 1U));
            }
        }
        off += got;
    }
    return ~crc;
}

static inline uint32_t odczytaj_u32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
          (static_cast<uint32_t>(p[1]) << 8) |
          (static_cast<uint32_t>(p[2]) << 16) |
          (static_cast<uint32_t>(p[3]) << 24);
}

uint32_t tapety_pamiec_crc32(const uint8_t* data, size_t len) {
    if (!data || !len) return 0;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xEDB88320U & -(crc & 1U));
        }
    }
    return ~crc;
}

bool tapety_nazwa_bezpieczna(const char* nazwa) {
    if (!nazwa || !nazwa[0] || nazwa[0] == '.') return false;
    size_t len = 0;
    while (nazwa[len]) {
        if (nazwa[len] == '/' || nazwa[len] == '\\' || nazwa[len] == '\r' ||
            nazwa[len] == '\n' || nazwa[len] == '"') return false;
        if (len >= 1 && nazwa[len] == '.' && nazwa[len - 1] == '.') return false;
        ++len;
        if (len > 120) return false;
    }
    return len >= 5;
}

bool znajdz_modul_tapet(uint64_t mbi, ModulMB2* wynik) {
    if (!wynik || mbi == 0) return false;
    wynik->poprawny = false;

    const volatile uint32_t* info = reinterpret_cast<const volatile uint32_t*>(mbi);
    const uint32_t calosc = info[0];
    if (calosc < 16 || mbi > UINT64_MAX - calosc) return false;

    uint64_t off = 8;
    while (off + sizeof(WpisTaguMB2) <= calosc) {
        const WpisTaguMB2* tag = reinterpret_cast<const WpisTaguMB2*>(mbi + off);
        if (tag->rozmiar < sizeof(WpisTaguMB2) || off + tag->rozmiar > calosc) return false;

        if (tag->typ == 3 /* MULTIBOOT_TAG_TYPE_MODULE */ && tag->rozmiar >= 16) {
            const uint32_t mod_start = *reinterpret_cast<const uint32_t*>(mbi + off + 8);
            const uint32_t mod_end   = *reinterpret_cast<const uint32_t*>(mbi + off + 12);
            if (mod_end > mod_start && mod_start >= 0x100000U) {
                const uint64_t start64 = static_cast<uint64_t>(mod_start);
                const uint64_t end64 = static_cast<uint64_t>(mod_end);
                const uint64_t sz = end64 - start64;
                if (sz >= 32 && end64 <= 0x100000000ULL) {
                    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(start64));
                    if (ptr[0] == 'B' && ptr[1] == 'T' && ptr[2] == 'P' && ptr[3] == 'K' && ptr[4] == '1') {
                        wynik->start = start64;
                        wynik->koniec = end64;
                        wynik->rozmiar = sz;
                        wynik->cmdline = reinterpret_cast<const char*>(mbi + off + 16);
                        wynik->poprawny = true;
                        return true;
                    }
                }
            }
        }

        if (tag->typ == 0 /* MULTIBOOT_TAG_TYPE_END */) break;
        off = (off + tag->rozmiar + 7ULL) & ~7ULL;
    }

    return false;
}

constexpr const char* TAPETY_INWENTARZ_CFG = "/ustawienia/tapetysystemowe.cfg";
constexpr size_t TAPETY_INWENTARZ_MAX = 64;

struct SystemowyWpisInwentarza {
    char nazwa[TAPETY_SCIEZKA_MAX];
    uint32_t rozmiar;
    uint32_t crc32;
};

struct WpisPakietu {
    char nazwa[TAPETY_SCIEZKA_MAX];
    uint32_t rozmiar;
    uint32_t crc32;
    const uint8_t* payload;
};

// Statyczne bufory w sekcji .bss zapobiegaja przepelnieniu 16 KiB stosu jadra.
char s_inwentarz_io_buf[16384];
SystemowyWpisInwentarza s_inwentarz_wpisy[TAPETY_INWENTARZ_MAX];
SystemowyWpisInwentarza s_inwentarz_poprzedni[TAPETY_INWENTARZ_MAX];
SystemowyWpisInwentarza s_inwentarz_biezacy[TAPETY_INWENTARZ_MAX];
WpisPakietu s_pakiet_wpisy[TAPETY_INWENTARZ_MAX];

size_t wczytaj_inwentarz(SystemowyWpisInwentarza* wpisy, size_t max_wpisow) {
    if (!wpisy || !max_wpisow) return 0;
    psf_metadane meta{};
    if (!pobierz_metadane_tworu(TAPETY_INWENTARZ_CFG, &meta) || meta.rozmiar == 0 || meta.rozmiar > sizeof(s_inwentarz_io_buf)) {
        return 0;
    }
    const uint32_t n = meta.rozmiar;
    for (size_t i = 0; i < sizeof(s_inwentarz_io_buf); ++i) s_inwentarz_io_buf[i] = 0;
    uint32_t got = 0;
    if (!czytaj_fragment_pliku(TAPETY_INWENTARZ_CFG, 0, s_inwentarz_io_buf, n, &got) || got != n) {
        return 0;
    }

    size_t count = 0;
    size_t pos = 0;
    while (pos < n && count < max_wpisow) {
        size_t end = pos;
        while (end < n && s_inwentarz_io_buf[end] != '\n') ++end;

        constexpr char prefix[] = "wpis=";
        constexpr size_t plen = sizeof(prefix) - 1;
        if (end - pos > plen) {
            bool matches_prefix = true;
            for (size_t k = 0; k < plen; ++k) {
                if (s_inwentarz_io_buf[pos + k] != prefix[k]) { matches_prefix = false; break; }
            }
            if (matches_prefix) {
                size_t cur = pos + plen;
                // 1. nazwa
                size_t p1 = cur;
                while (p1 < end && s_inwentarz_io_buf[p1] != '|') ++p1;
                if (p1 < end && s_inwentarz_io_buf[p1] == '|') {
                    size_t nlen = p1 - cur;
                    if (nlen > 0 && nlen < TAPETY_SCIEZKA_MAX) {
                        // 2. rozmiar
                        size_t p2 = p1 + 1;
                        size_t p3 = p2;
                        while (p3 < end && s_inwentarz_io_buf[p3] != '|') ++p3;
                        if (p3 < end && s_inwentarz_io_buf[p3] == '|') {
                            uint32_t rsz = 0;
                            bool rsz_ok = true;
                            for (size_t k = p2; k < p3; ++k) {
                                if (s_inwentarz_io_buf[k] < '0' || s_inwentarz_io_buf[k] > '9') { rsz_ok = false; break; }
                                rsz = rsz * 10 + (s_inwentarz_io_buf[k] - '0');
                            }
                            // 3. crc32 hex
                            uint32_t crc = 0;
                            bool crc_ok = true;
                            for (size_t k = p3 + 1; k < end; ++k) {
                                char ch = s_inwentarz_io_buf[k];
                                if (ch >= '0' && ch <= '9') crc = (crc << 4) | (ch - '0');
                                else if (ch >= 'a' && ch <= 'f') crc = (crc << 4) | (ch - 'a' + 10);
                                else if (ch >= 'A' && ch <= 'F') crc = (crc << 4) | (ch - 'A' + 10);
                                else if (ch == '\r') { /* ignoruj */ }
                                else { crc_ok = false; break; }
                            }
                            if (rsz_ok && crc_ok && rsz > 0) {
                                for (size_t k = 0; k < nlen; ++k) wpisy[count].nazwa[k] = s_inwentarz_io_buf[cur + k];
                                wpisy[count].nazwa[nlen] = '\0';
                                wpisy[count].rozmiar = rsz;
                                wpisy[count].crc32 = crc;
                                ++count;
                            }
                        }
                    }
                }
            }
        }
        pos = end + 1;
    }
    return count;
}

bool zapisz_inwentarz(const SystemowyWpisInwentarza* wpisy, size_t liczba) {
    if (!zapewnij_katalog("/ustawienia")) return false;
    for (size_t i = 0; i < sizeof(s_inwentarz_io_buf); ++i) s_inwentarz_io_buf[i] = 0;
    size_t pos = 0;
    for (size_t i = 0; i < liczba && pos + 256 < sizeof(s_inwentarz_io_buf); ++i) {
        if (!wpisy[i].nazwa[0]) continue;
        constexpr char prefix[] = "wpis=";
        for (size_t k = 0; prefix[k]; ++k) s_inwentarz_io_buf[pos++] = prefix[k];
        for (size_t k = 0; wpisy[i].nazwa[k]; ++k) s_inwentarz_io_buf[pos++] = wpisy[i].nazwa[k];
        s_inwentarz_io_buf[pos++] = '|';

        char rsz_buf[16]; size_t rsz_len = 0;
        uint32_t val = wpisy[i].rozmiar;
        do { rsz_buf[rsz_len++] = static_cast<char>('0' + (val % 10)); val /= 10; } while (val);
        while (rsz_len > 0) s_inwentarz_io_buf[pos++] = rsz_buf[--rsz_len];
        s_inwentarz_io_buf[pos++] = '|';

        uint32_t c = wpisy[i].crc32;
        for (int b = 7; b >= 0; --b) {
            uint32_t nib = (c >> (b * 4)) & 0x0FU;
            s_inwentarz_io_buf[pos++] = static_cast<char>(nib < 10 ? '0' + nib : 'A' + nib - 10);
        }
        s_inwentarz_io_buf[pos++] = '\n';
    }

    psf_metadane meta{};
    if (!pobierz_metadane_tworu(TAPETY_INWENTARZ_CFG, &meta)) {
        if (!utworz_plik(TAPETY_INWENTARZ_CFG)) return false;
    }
    if (!zapisz_do_pliku(TAPETY_INWENTARZ_CFG, s_inwentarz_io_buf, static_cast<uint32_t>(pos)) || !psf_flush_cache()) {
        return false;
    }
    return true;
}

void instaluj(uint64_t mbi) {
    if (!psf_czy_gotowy() || !zapewnij_katalog("/tapety")) {
        klog_serial("[TAPETA] BLAD utworzenia /tapety; start bedzie kontynuowany.");
        return;
    }

    ModulMB2 mod{};
    if (!znajdz_modul_tapet(mbi, &mod) || !mod.poprawny) {
        klog_serial("[TAPETA] Ostrzezenie: brak prawidlowego modulu Multiboot2 tapety.pak; zachowuje istniejace tapety.");
        return;
    }

    log_liczba("[TAPETA] Znaleziono modul tapety.pak, bajtow: ", mod.rozmiar);

    const uint8_t* p = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(mod.start));
    if (mod.rozmiar < 32) {
        klog_serial("[TAPETA-BLAD] Rozmiar modulu tapety.pak < 32 bajtow.");
        OdblokujZakres(mod.start, mod.rozmiar);
        return;
    }
    if (p[0] != 'B' || p[1] != 'T' || p[2] != 'P' || p[3] != 'K' || p[4] != '1') {
        klog_serial("[TAPETA-BLAD] Niepoprawny naglowek magic tapety.pak.");
        OdblokujZakres(mod.start, mod.rozmiar);
        return;
    }

    const uint32_t wersja = odczytaj_u32_le(p + 8);
    const uint32_t liczba_plikow = odczytaj_u32_le(p + 12);
    const uint32_t hdr_sz = odczytaj_u32_le(p + 16);
    const uint32_t tbl_sz = odczytaj_u32_le(p + 20);
    const uint32_t dat_sz = odczytaj_u32_le(p + 24);
    const uint32_t tot_sz = odczytaj_u32_le(p + 28);

    if (wersja != 1 || hdr_sz < 32 || hdr_sz > mod.rozmiar ||
        tbl_sz > mod.rozmiar - hdr_sz ||
        dat_sz > mod.rozmiar - hdr_sz - tbl_sz ||
        tot_sz > mod.rozmiar ||
        liczba_plikow > TAPETY_INWENTARZ_MAX) {
        klog_serial("[TAPETA-BLAD] Uszkodzona struktura naglowka tapety.pak.");
        OdblokujZakres(mod.start, mod.rozmiar);
        return;
    }

    log_liczba("[TAPETA] Pakiet tapety.pak, liczba plikow: ", liczba_plikow);

    for (size_t i = 0; i < TAPETY_INWENTARZ_MAX; ++i) {
        s_pakiet_wpisy[i] = {};
        s_inwentarz_poprzedni[i] = {};
        s_inwentarz_biezacy[i] = {};
    }
    size_t pakiet_liczba = 0;
    uint32_t toff = hdr_sz;
    const uint32_t tend = hdr_sz + tbl_sz;

    for (uint32_t i = 0; i < liczba_plikow && toff < tend; ++i) {
        if (tend - toff < 16) break;
        const uint32_t nlen = odczytaj_u32_le(p + toff);
        const uint32_t bsz  = odczytaj_u32_le(p + toff + 4);
        const uint32_t crc  = odczytaj_u32_le(p + toff + 8);
        const uint32_t poff = odczytaj_u32_le(p + toff + 12);
        toff += 16;

        const uint32_t npad = (4U - (nlen % 4U)) % 4U;
        if (nlen == 0 || nlen >= TAPETY_SCIEZKA_MAX - 10 || tend - toff < nlen + npad) {
            break;
        }

        char nazwa[TAPETY_SCIEZKA_MAX]{};
        for (uint32_t k = 0; k < nlen; ++k) nazwa[k] = static_cast<char>(p[toff + k]);
        nazwa[nlen] = '\0';
        toff += nlen + npad;

        if (!tapety_nazwa_bezpieczna(nazwa)) {
            log_sciezka("[TAPETA-BLAD] Niebezpieczna nazwa pliku w pakiecie: ", nazwa);
            continue;
        }

        if (poff < tend || poff > mod.rozmiar || bsz > mod.rozmiar - poff || bsz < 14 || bsz > TAPETY_PLIK_MAX) {
            log_sciezka("[TAPETA-BLAD] Nieprawidlowy zakres payloadu dla: ", nazwa);
            continue;
        }

        BtpInfo info{};
        if (btpsprawdzplik(p + poff, bsz, &info) != BTPOK) {
            log_sciezka("[TAPETA-BLAD] Niepoprawny plik BTP1 w pakiecie: ", nazwa);
            continue;
        }

        const uint32_t obl_crc = tapety_pamiec_crc32(p + poff, bsz);
        if (obl_crc != crc) {
            log_sciezka("[TAPETA-BLAD] Niezgodnosc CRC32 w pakiecie dla: ", nazwa);
            continue;
        }

        tapety_kopiuj(s_pakiet_wpisy[pakiet_liczba].nazwa, nazwa, sizeof(s_pakiet_wpisy[0].nazwa));
        s_pakiet_wpisy[pakiet_liczba].rozmiar = bsz;
        s_pakiet_wpisy[pakiet_liczba].crc32 = crc;
        s_pakiet_wpisy[pakiet_liczba].payload = p + poff;
        ++pakiet_liczba;
    }

    const size_t poprzednia_liczba = wczytaj_inwentarz(s_inwentarz_poprzedni, TAPETY_INWENTARZ_MAX);

    // Krok 1: Wykryj wycofane tapety systemowe (obecne w poprzednim inwentarzu, nieobecne w biezacym pakiecie)
    for (size_t i = 0; i < poprzednia_liczba; ++i) {
        bool nadal_w_pakiecie = false;
        for (size_t j = 0; j < pakiet_liczba; ++j) {
            if (tapety_rowne(s_inwentarz_poprzedni[i].nazwa, s_pakiet_wpisy[j].nazwa)) {
                nadal_w_pakiecie = true;
                break;
            }
        }
        if (!nadal_w_pakiecie) {
            char sciezka[TAPETY_SCIEZKA_MAX]{};
            size_t sp = 0;
            constexpr char prefix[] = "/tapety/";
            for (; prefix[sp] && sp + 1 < sizeof(sciezka); ++sp) sciezka[sp] = prefix[sp];
            for (size_t k = 0; s_inwentarz_poprzedni[i].nazwa[k] && sp + 1 < sizeof(sciezka); ++k) sciezka[sp++] = s_inwentarz_poprzedni[i].nazwa[k];
            sciezka[sp] = '\0';

            psf_metadane meta{};
            if (pobierz_metadane_tworu(sciezka, &meta)) {
                if (meta.rozmiar == s_inwentarz_poprzedni[i].rozmiar) {
                    const uint32_t plik_crc = tapety_plik_crc32(sciezka, meta.rozmiar);
                    if (plik_crc == s_inwentarz_poprzedni[i].crc32) {
                        (void)usun_twor(sciezka);
                        (void)psf_flush_cache();
                        log_sciezka("[TAPETA] Usunieto wycofana tapete systemowa: ", sciezka);
                    } else {
                        log_sciezka("[TAPETA] Zachowano zmodyfikowana tapete uzytkownika: ", sciezka);
                    }
                } else {
                    log_sciezka("[TAPETA] Zachowano zmodyfikowana tapete uzytkownika: ", sciezka);
                }
            }
        }
    }

    // Krok 2: Instalacja brakujacych i zachowanie istniejacych tapet z biezacego pakietu
    uint32_t nowe = 0, zachowane = 0;
    size_t biezaca_liczba = 0;

    for (size_t i = 0; i < pakiet_liczba; ++i) {
        char sciezka[TAPETY_SCIEZKA_MAX]{};
        size_t sp = 0;
        constexpr char prefix[] = "/tapety/";
        for (; prefix[sp] && sp + 1 < sizeof(sciezka); ++sp) sciezka[sp] = prefix[sp];
        for (size_t k = 0; s_pakiet_wpisy[i].nazwa[k] && sp + 1 < sizeof(sciezka); ++k) sciezka[sp++] = s_pakiet_wpisy[i].nazwa[k];
        sciezka[sp] = '\0';

        psf_metadane meta{};
        if (pobierz_metadane_tworu(sciezka, &meta)) {
            ++zachowane;
        } else {
            if (!utworz_plik(sciezka)) {
                log_sciezka("[TAPETA] BLAD tworzenia pliku ", sciezka);
                continue;
            }
            if (!zapisz_do_pliku(sciezka, reinterpret_cast<const char*>(s_pakiet_wpisy[i].payload), s_pakiet_wpisy[i].rozmiar) || !psf_flush_cache()) {
                log_sciezka("[TAPETA] BLAD zapisu (I/O lub brak miejsca) ", sciezka);
                (void)usun_twor(sciezka);
                continue;
            }
            ++nowe;
        }

        if (biezaca_liczba < TAPETY_INWENTARZ_MAX) {
            tapety_kopiuj(s_inwentarz_biezacy[biezaca_liczba].nazwa, s_pakiet_wpisy[i].nazwa, sizeof(s_inwentarz_biezacy[0].nazwa));
            s_inwentarz_biezacy[biezaca_liczba].rozmiar = s_pakiet_wpisy[i].rozmiar;
            s_inwentarz_biezacy[biezaca_liczba].crc32 = s_pakiet_wpisy[i].crc32;
            ++biezaca_liczba;
        }
    }

    (void)zapisz_inwentarz(s_inwentarz_biezacy, biezaca_liczba);
    log_liczba("[TAPETA] Zainstalowano brakujacych: ", nowe);
    log_liczba("[TAPETA] Zachowano istniejacych: ", zachowane);

    // Krok 3: Zwolnienie pamieci fizycznej modulu tapety.pak
    OdblokujZakres(mod.start, mod.rozmiar);
    klog_serial("[PMM] Pamiec fizyczna modulu tapety.pak zwolniona do puli PMM.");
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

bool zastosuj(const TapetyKonfiguracja& c, bool cicho, bool publikuj = true) {
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
    if (publikuj) grafika_publikuj_tapete();
    if (!cicho) {
        log_sciezka("[TAPETA] Wybrano ", c.sciezka);
        log_sciezka("[TAPETA] Tryb ", tapety_nazwa_trybu(c.tryb));
#if BURSZTYN_DIAG_TAPETY
        log_liczba("[TAPETA] BTP szerokosc ", info.szerokosc);
        log_liczba("[TAPETA] BTP wysokosc ", info.wysokosc);
        klog_serial(info.kompresja ? "[TAPETA] RLE" : "[TAPETA] RAW");
        log_liczba("[TAPETA] Odczyt us ", statystyki.odczyt_us);
        log_liczba("[TAPETA] Dekodowanie us ", statystyki.dekoduj_us);
        log_liczba("[TAPETA] Przygotowanie us ", statystyki.skaluj_us);
#endif
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
    for (size_t i = 0; i < TAPETY_INWENTARZ_MAX; ++i) s_inwentarz_wpisy[i] = {};
    const size_t ile = wczytaj_inwentarz(s_inwentarz_wpisy, TAPETY_INWENTARZ_MAX);

    if (grupa == 3) {
        if (!czy_katalog_istnieje("/tapety")) ++bledy;
        for (size_t i = 0; i < ile; ++i) {
            char path[TAPETY_SCIEZKA_MAX]{};
            size_t p = 0;
            constexpr char prefix[] = "/tapety/";
            for (; prefix[p] && p + 1 < sizeof(path); ++p) path[p] = prefix[p];
            for (size_t k = 0; s_inwentarz_wpisy[i].nazwa[k] && p + 1 < sizeof(path); ++k)
                path[p++] = s_inwentarz_wpisy[i].nazwa[k];
            path[p] = '\0';
            BtpInfo info{}; uint32_t* src = nullptr;
            if (wczytaj(path, &info, &src, true)) ++ok;
            else ++bledy;
        }
        if (ile == 0) {
            BtpInfo info{}; uint32_t* src = nullptr;
            if (wczytaj("/tapety/bursztyn01.btp", &info, &src, true)) ++ok;
        }
    } else if (grupa == 6) {
        const TapetyKonfiguracja stara = aktualna;
        size_t cap = 0;
        const uint32_t* adres = grafika_bufor_tapety(&cap);
        const size_t stare_bajty = cap;
        przed = heap_zajete_bajty();
        for (uint32_t i = 0; i < 100; ++i) {
            TapetyKonfiguracja c = tapety_domyslna();
            if (ile > 0) {
                const auto& z = s_inwentarz_wpisy[i % ile];
                size_t p = 0;
                constexpr char prefix[] = "/tapety/";
                for (; prefix[p] && p + 1 < sizeof(c.sciezka); ++p) c.sciezka[p] = prefix[p];
                for (size_t k = 0; z.nazwa[k] && p + 1 < sizeof(c.sciezka); ++k) c.sciezka[p++] = z.nazwa[k];
                c.sciezka[p] = '\0';
            }
            c.tryb = static_cast<TrybSkalowaniaObrazu>(i % 3);
            if (zastosuj(c, true, false) && grafika_bufor_tapety(&cap) == adres && cap == stare_bajty) ++ok;
            else ++bledy;
        }
        if (stara.sciezka[0]) {
            if (!zastosuj(stara, true, true)) ++bledy;
        } else {
            if (statystyki.zrodlo == TAPETY_BMP) {
                wczytaj_tapete_z_dysku();
            } else {
                grafika_tapeta_solid();
            }
        }
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

void tapety_inicjalizuj(uint64_t mbi) {
    if (mbi == 0) mbi = g_multiboot_info_ptr;
    aktualna = tapety_domyslna();
    instaluj(mbi);
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
        [](const TapetyKonfiguracja& c) {
            if (zastosuj(c, false)) return true;
            constexpr const char* domyslny_plik = "/tapety/bursztyn01.btp";
            if (!tapety_rowne(c.sciezka, domyslny_plik)) {
                TapetyKonfiguracja dom{};
                tapety_kopiuj(dom.sciezka, domyslny_plik, sizeof(dom.sciezka));
                dom.tryb = c.tryb;
                if (zastosuj(dom, false)) return true;
            }
            for (size_t i = 0; i < TAPETY_INWENTARZ_MAX; ++i) s_inwentarz_wpisy[i] = {};
            const size_t ile = wczytaj_inwentarz(s_inwentarz_wpisy, TAPETY_INWENTARZ_MAX);
            for (size_t i = 0; i < ile; ++i) {
                char path[TAPETY_SCIEZKA_MAX]{};
                size_t p = 0;
                constexpr char prefix[] = "/tapety/";
                for (; prefix[p] && p + 1 < sizeof(path); ++p) path[p] = prefix[p];
                for (size_t k = 0; s_inwentarz_wpisy[i].nazwa[k] && p + 1 < sizeof(path); ++k)
                    path[p++] = s_inwentarz_wpisy[i].nazwa[k];
                path[p] = '\0';
                if (!tapety_rowne(path, c.sciezka) && !tapety_rowne(path, domyslny_plik)) {
                    TapetyKonfiguracja alt{};
                    tapety_kopiuj(alt.sciezka, path, sizeof(alt.sciezka));
                    alt.tryb = c.tryb;
                    if (zastosuj(alt, false)) return true;
                }
            }
            return false;
        },
        []() { klog_serial("[TAPETA] Fallback BMP LBA 10."); wczytaj_tapete_z_dysku(); return grafika_czy_tapeta(); },
        []() { klog_serial("[TAPETA] Fallback jednolity kolor."); grafika_tapeta_solid(); });
    if (statystyki.zrodlo != TAPETY_BTP) aktualna.sciezka[0] = 0;
    TapetyOdpowiedz s{}; snapshot(&s);
#if BURSZTYN_DIAG_TAPETY
    log_liczba("[TAPETA] Bufor aktywny bajty ", s.bufor_bajty);
    log_liczba("[TAPETA] Scratch wspolny BMP/BTP bajty ", s.scratch_bajty);
#endif
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
