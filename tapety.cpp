#include "tapety.h"
#include "instalator.h"
#include "grafika.h"
#include "heap.h"
#include "psf.h"
#include "narzedzia/tapety-btp/tapetybtp.h"
#include "sterowniki/czas/hpet.h"

#include "pamiec.h"
#include "bufory_grafiki.h"

#ifndef BURSZTYNDIAGTAPETYPERF
#define BURSZTYNDIAGTAPETYPERF 0
#endif

extern uint64_t g_multiboot_info_ptr;
extern bool skopiuj_z_przestrzeni_uzytkownika(void*, const void*, size_t);
extern bool czy_bezpieczny_zakres_uzytkownika_do_odczytu(const void*, size_t);

namespace {
TapetyKonfiguracja aktualna{};
TapetyOdpowiedz statystyki{};
bool bufory_gotowe = false;
uint32_t jpeg_w = 0, jpeg_h = 0;
TapetyKonfiguracja jpeg_do_przywrocenia{};

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
            const char* cmdline = reinterpret_cast<const char*>(mbi + off + 16);
            // Multiboot2 nie wymaga adresu >= 1 MiB. Maly pakiet GRUB moze
            // legalnie umiescic w niskiej pamieci; PMM rezerwuje zakres z tagu.
            if (mod_start != 0 && mod_end > mod_start) {
                const uint64_t start64 = static_cast<uint64_t>(mod_start);
                const uint64_t end64 = static_cast<uint64_t>(mod_end);
                const uint64_t sz = end64 - start64;
                if (sz >= 32 && end64 <= 0x100000000ULL) {
                    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(start64));
                    if (ptr[0] == 'B' && ptr[1] == 'T' && ptr[2] == 'P' && ptr[3] == 'K' && ptr[4] == '1') {
                        wynik->start = start64;
                        wynik->koniec = end64;
                        wynik->rozmiar = sz;
                        wynik->cmdline = cmdline;
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
constexpr const char* TAPETY_MANIFEST_CFG = "/tapety/manifest.cfg";
constexpr size_t TAPETY_INWENTARZ_MAX = 64;
constexpr size_t TAPETY_PAKIET_MAX = TAPETY_INWENTARZ_MAX * 2;
constexpr uint32_t TAPETY_MINIATURA_W = TAPETY_MINI_W;
constexpr uint32_t TAPETY_MINIATURA_H = TAPETY_MINI_H;

struct SystemowyWpisInwentarza {
    char nazwa[TAPETY_SCIEZKA_MAX];       // Kanoniczna nazwa (np. bursztyn01.btp, wp_...jpg)
    char wyswietlana[TAPETY_SCIEZKA_MAX];  // Nazwa wyswietlana UTF-8 (np. Żółta łąka.jpg)
    uint32_t rozmiar;
    uint32_t crc32;
};

struct WpisPakietu {
    char nazwa[TAPETY_SCIEZKA_MAX];       // Kanoniczna nazwa
    char wyswietlana[TAPETY_SCIEZKA_MAX];  // Nazwa wyswietlana UTF-8
    uint32_t rozmiar;
    uint32_t crc32;
    const uint8_t* payload;
    bool miniatura;
};

// Statyczne bufory w sekcji .bss zapobiegaja przepelnieniu 16 KiB stosu jadra.
char s_inwentarz_io_buf[16384];
SystemowyWpisInwentarza s_inwentarz_wpisy[TAPETY_INWENTARZ_MAX];
SystemowyWpisInwentarza s_inwentarz_poprzedni[TAPETY_INWENTARZ_MAX];
SystemowyWpisInwentarza s_inwentarz_biezacy[TAPETY_INWENTARZ_MAX];
WpisPakietu s_pakiet_wpisy[TAPETY_PAKIET_MAX];

bool tapety_czy_nazwa_miniatury(const char* nazwa) {
    if (!nazwa) return false;
    constexpr char prefix[] = "miniatury/";
    size_t p = 0;
    for (; prefix[p]; ++p) if (nazwa[p] != prefix[p]) return false;
    const char* leaf = nazwa + p;
    return tapeta_sprawdz_nazwe_utf8(leaf) && tapety_koncowka(leaf, ".btp");
}

bool tapety_miniatura_dla_nazwy(const char* kanoniczna, char* out, size_t cap) {
    if (!kanoniczna || !out || cap == 0) return false;
    constexpr char prefix[] = "miniatury/";
    size_t p = 0;
    for (; prefix[p] && p + 1 < cap; ++p) out[p] = prefix[p];
    size_t dot = 0;
    while (kanoniczna[dot] && kanoniczna[dot] != '.') ++dot;
    if (!dot || !kanoniczna[dot]) return false;
    for (size_t i = 0; i < dot && p + 1 < cap; ++i) out[p++] = kanoniczna[i];
    constexpr char ext[] = ".btp";
    for (size_t i = 0; ext[i] && p + 1 < cap; ++i) out[p++] = ext[i];
    out[p] = '\0';
    return tapety_czy_nazwa_miniatury(out);
}

bool tapety_sciezka_z_pakietu(const WpisPakietu& e, char* out, size_t cap) {
    if (!out || cap == 0 || !e.nazwa[0]) return false;
    size_t p = 0;
    constexpr char prefix[] = "/tapety/";
    for (; prefix[p] && p + 1 < cap; ++p) out[p] = prefix[p];
    for (size_t i = 0; e.nazwa[i] && p + 1 < cap; ++i) out[p++] = e.nazwa[i];
    out[p] = 0;
    return true;
}

bool tapety_sciezka_miniatury_dla_tapety(const char* kanoniczna, char* out, size_t cap) {
    char mini[TAPETY_SCIEZKA_MAX]{};
    if (!tapety_miniatura_dla_nazwy(kanoniczna, mini, sizeof(mini))) return false;
    size_t p = 0;
    constexpr char prefix[] = "/tapety/";
    for (; prefix[p] && p + 1 < cap; ++p) out[p] = prefix[p];
    for (size_t i = 0; mini[i] && p + 1 < cap; ++i) out[p++] = mini[i];
    out[p] = 0;
    return true;
}

bool tapety_miniatura_btp_poprawna(const uint8_t* dane, uint32_t rozmiar) {
    if (!dane || rozmiar < 14 || rozmiar > TAPETY_PLIK_MAX) return false;
    BtpInfo info{};
    if (btpsprawdzplik(dane, rozmiar, &info) != BTPOK) return false;
    return info.szerokosc == TAPETY_MINIATURA_W && info.wysokosc == TAPETY_MINIATURA_H;
}

bool tapety_miniatura_plik_poprawny(const char* path) {
    const uint32_t n = rozmiar_pliku(path);
    if (n < 14 || n > TAPETY_PLIK_MAX) return false;
    uint8_t header[128]{};
    uint32_t got = 0;
    const uint32_t ile = n < sizeof(header) ? n : static_cast<uint32_t>(sizeof(header));
    if (!czytaj_fragment_pliku(path, 0, reinterpret_cast<char*>(header), ile, &got) || got != ile) return false;
    BtpInfo info{};
    if (btpnaglowek(header, ile, &info) != BTPOK) return false;
    return info.szerokosc == TAPETY_MINIATURA_W && info.wysokosc == TAPETY_MINIATURA_H;
}

size_t wczytaj_inwentarz(SystemowyWpisInwentarza* wpisy, size_t max_wpisow, char* domyslna_out = nullptr, size_t domyslna_cap = 0) {
    if (!wpisy || !max_wpisow) return 0;
    if (domyslna_out && domyslna_cap) domyslna_out[0] = '\0';
    psf_metadane meta{};
    const char* zrodlo_cfg = TAPETY_INWENTARZ_CFG;
    if (!pobierz_metadane_tworu(zrodlo_cfg, &meta) || meta.rozmiar == 0) {
        zrodlo_cfg = TAPETY_MANIFEST_CFG;
        if (!pobierz_metadane_tworu(zrodlo_cfg, &meta) || meta.rozmiar == 0) {
            return 0;
        }
    }
    if (meta.rozmiar > sizeof(s_inwentarz_io_buf)) return 0;
    const uint32_t n = meta.rozmiar;
    for (size_t i = 0; i < sizeof(s_inwentarz_io_buf); ++i) s_inwentarz_io_buf[i] = 0;
    uint32_t got = 0;
    if (!czytaj_fragment_pliku(zrodlo_cfg, 0, s_inwentarz_io_buf, n, &got) || got != n) {
        return 0;
    }

    size_t count = 0;
    size_t pos = 0;
    while (pos < n && count < max_wpisow) {
        size_t end = pos;
        while (end < n && s_inwentarz_io_buf[end] != '\n') ++end;

        // 1. Sprawdz linie default= lub domyslna=
        constexpr char def_prefix[] = "default=";
        constexpr size_t def_len = sizeof(def_prefix) - 1;
        constexpr char dom_prefix[] = "domyslna=";
        constexpr size_t dom_len = sizeof(dom_prefix) - 1;
        if (domyslna_out && domyslna_cap > 0 && domyslna_out[0] == '\0') {
            size_t p_prefix = 0;
            if (end - pos > def_len) {
                bool match = true;
                for (size_t k = 0; k < def_len; ++k) {
                    if (s_inwentarz_io_buf[pos + k] != def_prefix[k]) { match = false; break; }
                }
                if (match) p_prefix = def_len;
            }
            if (!p_prefix && end - pos > dom_len) {
                bool match = true;
                for (size_t k = 0; k < dom_len; ++k) {
                    if (s_inwentarz_io_buf[pos + k] != dom_prefix[k]) { match = false; break; }
                }
                if (match) p_prefix = dom_len;
            }
            if (p_prefix > 0) {
                size_t d_start = pos + p_prefix;
                size_t d_end = end;
                while (d_end > d_start && (s_inwentarz_io_buf[d_end - 1] == '\r' || s_inwentarz_io_buf[d_end - 1] == ' ' || s_inwentarz_io_buf[d_end - 1] == '\t')) --d_end;
                while (d_start < d_end && (s_inwentarz_io_buf[d_start] == ' ' || s_inwentarz_io_buf[d_start] == '\t')) ++d_start;
                size_t dlen = d_end - d_start;
                if (dlen > 0 && dlen < domyslna_cap) {
                    for (size_t k = 0; k < dlen; ++k) domyslna_out[k] = s_inwentarz_io_buf[d_start + k];
                    domyslna_out[dlen] = '\0';
                }
            }
        }

        // 2. Sprawdz linie wpis=
        constexpr char prefix[] = "wpis=";
        constexpr size_t plen = sizeof(prefix) - 1;
        if (end - pos > plen) {
            bool matches_prefix = true;
            for (size_t k = 0; k < plen; ++k) {
                if (s_inwentarz_io_buf[pos + k] != prefix[k]) { matches_prefix = false; break; }
            }
            if (matches_prefix) {
                size_t cur = pos + plen;
                // 1. nazwa kanoniczna
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
                            size_t p4 = p3 + 1;
                            size_t p5 = p4;
                            while (p5 < end && s_inwentarz_io_buf[p5] != '|') ++p5;
                            uint32_t crc = 0;
                            bool crc_ok = true;
                            for (size_t k = p4; k < p5; ++k) {
                                char ch = s_inwentarz_io_buf[k];
                                if (ch >= '0' && ch <= '9') crc = (crc << 4) | (ch - '0');
                                else if (ch >= 'a' && ch <= 'f') crc = (crc << 4) | (ch - 'a' + 10);
                                else if (ch >= 'A' && ch <= 'F') crc = (crc << 4) | (ch - 'A' + 10);
                                else if (ch == '\r') { /* ignoruj */ }
                                else { crc_ok = false; break; }
                            }
                            // 4. opcjonalna nazwa wyswietlana
                            char wyswietlana[TAPETY_SCIEZKA_MAX]{};
                            size_t wlen = 0;
                            if (p5 < end && s_inwentarz_io_buf[p5] == '|') {
                                size_t w_start = p5 + 1;
                                size_t w_end = end;
                                if (w_end > w_start && s_inwentarz_io_buf[w_end - 1] == '\r') --w_end;
                                wlen = w_end - w_start;
                                if (wlen >= sizeof(wyswietlana)) wlen = sizeof(wyswietlana) - 1;
                                for (size_t k = 0; k < wlen; ++k) {
                                    wyswietlana[k] = s_inwentarz_io_buf[w_start + k];
                                }
                                wyswietlana[wlen] = '\0';
                            }
                            if (wlen == 0) {
                                for (size_t k = 0; k < nlen; ++k) wyswietlana[k] = s_inwentarz_io_buf[cur + k];
                                wyswietlana[nlen] = '\0';
                                wlen = nlen;
                            }
                            if (rsz_ok && crc_ok && rsz > 0) {
                                for (size_t k = 0; k < nlen; ++k) wpisy[count].nazwa[k] = s_inwentarz_io_buf[cur + k];
                                wpisy[count].nazwa[nlen] = '\0';
                                for (size_t k = 0; k <= wlen; ++k) wpisy[count].wyswietlana[k] = wyswietlana[k];
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
    if (domyslna_out && domyslna_cap > 0 && domyslna_out[0] == '\0' && count > 0) {
        tapety_kopiuj(domyslna_out, wpisy[0].nazwa, domyslna_cap);
    }
    return count;
}

bool zapisz_inwentarz(const SystemowyWpisInwentarza* wpisy, size_t liczba, const char* domyslna = nullptr) {
    if (!zapewnij_katalog("/ustawienia") || !zapewnij_katalog("/tapety")) return false;
    for (size_t i = 0; i < sizeof(s_inwentarz_io_buf); ++i) s_inwentarz_io_buf[i] = 0;
    size_t pos = 0;

    const char* def_name = (domyslna && domyslna[0]) ? domyslna : ((liczba > 0 && wpisy[0].nazwa[0]) ? wpisy[0].nazwa : nullptr);
    if (def_name) {
        constexpr char def_prefix[] = "default=";
        for (size_t k = 0; def_prefix[k]; ++k) s_inwentarz_io_buf[pos++] = def_prefix[k];
        for (size_t k = 0; def_name[k] && pos + 2 < sizeof(s_inwentarz_io_buf); ++k) s_inwentarz_io_buf[pos++] = def_name[k];
        s_inwentarz_io_buf[pos++] = '\n';
    }

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
        s_inwentarz_io_buf[pos++] = '|';
        const char* w = wpisy[i].wyswietlana[0] ? wpisy[i].wyswietlana : wpisy[i].nazwa;
        for (size_t k = 0; w[k] && pos + 2 < sizeof(s_inwentarz_io_buf); ++k) {
            s_inwentarz_io_buf[pos++] = w[k];
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

    psf_metadane meta_manifest{};
    if (!pobierz_metadane_tworu(TAPETY_MANIFEST_CFG, &meta_manifest)) {
        if (!utworz_plik(TAPETY_MANIFEST_CFG)) return false;
    }
    if (!zapisz_do_pliku(TAPETY_MANIFEST_CFG, s_inwentarz_io_buf, static_cast<uint32_t>(pos)) || !psf_flush_cache()) {
        return false;
    }
    return true;
}

uint32_t wymagane_tapety=0, zainstalowane_tapety=0;
size_t instalacja_pakiet_liczba=0,instalacja_indeks=0,instalacja_biezaca_liczba=0;
uint32_t instalacja_offset=0;
uint32_t instalacja_crc_offset=0,instalacja_crc=~0U;
uint64_t instalacja_bajty=0,instalacja_bajty_razem=0;
bool instalacja_przygotowana=false,instalacja_blad=false;
void instaluj(uint64_t mbi,bool tylko_przygotuj=false) {
    wymagane_tapety=0;
    zainstalowane_tapety=0;
    if(tylko_przygotuj){
        instalacja_przygotowana=false;instalacja_blad=false;instalacja_pakiet_liczba=0;
        instalacja_bajty=instalacja_bajty_razem=0;
    }
    if (!psf_czy_gotowy() || !zapewnij_katalog("/tapety") || !zapewnij_katalog("/tapety/miniatury")) {
        klog_serial("[TAPETA] BLAD utworzenia /tapety; start bedzie kontynuowany.");
        return;
    }

    ModulMB2 mod{};
    if (!znajdz_modul_tapet(mbi, &mod) || !mod.poprawny) {
        const bool installed_resources = instalator_system_zainstalowany() &&
                                         tapety_weryfikuj_instalacje();
        klog_serial(tapety_komunikat_braku_pakietu(
            instalator_system_zainstalowany(),installed_resources));
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
    wymagane_tapety=0;
    const uint32_t hdr_sz = odczytaj_u32_le(p + 16);
    const uint32_t tbl_sz = odczytaj_u32_le(p + 20);
    const uint32_t dat_sz = odczytaj_u32_le(p + 24);
    const uint32_t tot_sz = odczytaj_u32_le(p + 28);

    if (wersja != 1 || hdr_sz < 32 || hdr_sz > mod.rozmiar ||
        tbl_sz > mod.rozmiar - hdr_sz ||
        dat_sz > mod.rozmiar - hdr_sz - tbl_sz ||
        tot_sz > mod.rozmiar ||
        liczba_plikow > TAPETY_PAKIET_MAX) {
        klog_serial("[TAPETA-BLAD] Uszkodzona struktura naglowka tapety.pak.");
        OdblokujZakres(mod.start, mod.rozmiar);
        return;
    }

    log_liczba("[TAPETA] Pakiet tapety.pak, liczba plikow: ", liczba_plikow);

    for (size_t i = 0; i < TAPETY_PAKIET_MAX; ++i) {
        s_pakiet_wpisy[i] = {};
    }
    for (size_t i = 0; i < TAPETY_INWENTARZ_MAX; ++i) {
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

        char raw_nazwa[TAPETY_SCIEZKA_MAX]{};
        for (uint32_t k = 0; k < nlen; ++k) raw_nazwa[k] = static_cast<char>(p[toff + k]);
        raw_nazwa[nlen] = '\0';
        toff += nlen + npad;

        char canon[TAPETY_SCIEZKA_MAX]{};
        bool jest_miniatura = false;
        if (tapety_czy_nazwa_miniatury(raw_nazwa)) {
            tapety_kopiuj(canon, raw_nazwa, sizeof(canon));
            jest_miniatura = true;
        } else {
            if (!tapeta_sprawdz_nazwe_utf8(raw_nazwa)) {
                log_sciezka("[TAPETA-BLAD] Niepoprawna nazwa pliku w pakiecie: ", raw_nazwa);
                continue;
            }
            if (!tapeta_kanoniczna_nazwa(raw_nazwa, canon, sizeof(canon))) {
                log_sciezka("[TAPETA-BLAD] Blad kanonizacji nazwy: ", raw_nazwa);
                continue;
            }
        }

        bool duplikat = false;
        for (size_t k = 0; k < pakiet_liczba; ++k) {
            if (tapety_rowne(s_pakiet_wpisy[k].nazwa, canon)) {
                duplikat = true;
                break;
            }
        }
        if (duplikat) {
            log_sciezka("[TAPETA-BLAD] Wykryto zduplikowana nazwe kanoniczna w pakiecie: ", canon);
            continue;
        }

        if (poff < tend || poff > mod.rozmiar || bsz > mod.rozmiar - poff || bsz < 4 || bsz > TAPETY_PLIK_MAX) {
            log_sciezka("[TAPETA-BLAD] Nieprawidlowy zakres payloadu dla: ", raw_nazwa);
            continue;
        }

        const bool is_btp = tapety_koncowka(canon, ".btp");
        const bool is_jpeg = tapety_koncowka(canon, ".jpg") || tapety_koncowka(canon, ".jpeg");
        const bool is_png = tapety_koncowka(canon, ".png");

        if (jest_miniatura) {
            if (!tapety_miniatura_btp_poprawna(p + poff, bsz)) {
                log_sciezka("[TAPETA-BLAD] Niepoprawna miniatura BTP w pakiecie: ", raw_nazwa);
                continue;
            }
        } else if (is_btp) {
            if (bsz < 14) {
                log_sciezka("[TAPETA-BLAD] Zbyt maly plik BTP1 w pakiecie: ", raw_nazwa);
                continue;
            }
            BtpInfo info{};
            if (btpsprawdzplik(p + poff, bsz, &info) != BTPOK) {
                log_sciezka("[TAPETA-BLAD] Niepoprawny plik BTP1 w pakiecie: ", raw_nazwa);
                continue;
            }
        } else if (is_jpeg) {
            if (bsz < 4 || p[poff] != 0xFF || p[poff + 1] != 0xD8) {
                log_sciezka("[TAPETA-BLAD] Niepoprawny plik JPEG w pakiecie: ", raw_nazwa);
                continue;
            }
        } else if (is_png) {
            if (bsz < 8 || p[poff] != 0x89 || p[poff + 1] != 'P' || p[poff + 2] != 'N' || p[poff + 3] != 'G' ||
                p[poff + 4] != 0x0D || p[poff + 5] != 0x0A || p[poff + 6] != 0x1A || p[poff + 7] != 0x0A) {
                log_sciezka("[TAPETA-BLAD] Niepoprawny plik PNG w pakiecie: ", raw_nazwa);
                continue;
            }
        } else {
            log_sciezka("[TAPETA-BLAD] Nieznany format pliku w pakiecie: ", raw_nazwa);
            continue;
        }

        const uint32_t obl_crc = tylko_przygotuj ? crc : tapety_pamiec_crc32(p + poff, bsz);
        if (obl_crc != crc) {
            log_sciezka("[TAPETA-BLAD] Niezgodnosc CRC32 w pakiecie dla: ", raw_nazwa);
            continue;
        }

        tapety_kopiuj(s_pakiet_wpisy[pakiet_liczba].nazwa, canon, sizeof(s_pakiet_wpisy[0].nazwa));
        tapety_kopiuj(s_pakiet_wpisy[pakiet_liczba].wyswietlana, raw_nazwa, sizeof(s_pakiet_wpisy[0].wyswietlana));
        s_pakiet_wpisy[pakiet_liczba].rozmiar = bsz;
        s_pakiet_wpisy[pakiet_liczba].crc32 = crc;
        s_pakiet_wpisy[pakiet_liczba].payload = p + poff;
        s_pakiet_wpisy[pakiet_liczba].miniatura = jest_miniatura;
        if (!jest_miniatura) ++wymagane_tapety;
        ++pakiet_liczba;
    }

    for (size_t i = 0; i < pakiet_liczba; ++i) {
        if (s_pakiet_wpisy[i].miniatura) continue;
        char oczekiwana[TAPETY_SCIEZKA_MAX]{};
        bool znaleziona = false;
        if (!tapety_miniatura_dla_nazwy(s_pakiet_wpisy[i].nazwa, oczekiwana, sizeof(oczekiwana))) continue;
        for (size_t j = 0; j < pakiet_liczba; ++j) {
            if (s_pakiet_wpisy[j].miniatura && tapety_rowne(s_pakiet_wpisy[j].nazwa, oczekiwana)) {
                znaleziona = true;
                break;
            }
        }
        if (!znaleziona) log_sciezka("[TAPETA-BLAD] Brak miniatury w pakiecie dla: ", s_pakiet_wpisy[i].nazwa);
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

    if(tylko_przygotuj){
        instalacja_pakiet_liczba=pakiet_liczba;instalacja_indeks=0;instalacja_biezaca_liczba=0;
        instalacja_offset=0;instalacja_crc_offset=0;instalacja_crc=~0U;
        instalacja_bajty=0;instalacja_bajty_razem=0;instalacja_blad=false;
        for(size_t i=0;i<pakiet_liczba;++i){
            if(instalacja_bajty_razem>UINT64_MAX-s_pakiet_wpisy[i].rozmiar){instalacja_blad=true;break;}
            instalacja_bajty_razem+=s_pakiet_wpisy[i].rozmiar;
        }
        instalacja_przygotowana=!instalacja_blad&&pakiet_liczba==liczba_plikow&&pakiet_liczba!=0;
        return;
    }

    // Krok 2: Instalacja brakujacych i zachowanie istniejacych tapet z biezacego pakietu
    uint32_t nowe = 0, zachowane = 0;
    size_t biezaca_liczba = 0;

    for (size_t i = 0; i < pakiet_liczba; ++i) {
        char sciezka[TAPETY_SCIEZKA_MAX]{};
        if (!tapety_sciezka_z_pakietu(s_pakiet_wpisy[i], sciezka, sizeof(sciezka))) continue;

        psf_metadane meta{};
        if (pobierz_metadane_tworu(sciezka, &meta) && meta.rozmiar == s_pakiet_wpisy[i].rozmiar) {
            ++zachowane;
        } else {
            if (meta.rozmiar != 0) (void)usun_twor(sciezka);
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

        if (!s_pakiet_wpisy[i].miniatura && biezaca_liczba < TAPETY_INWENTARZ_MAX) {
            tapety_kopiuj(s_inwentarz_biezacy[biezaca_liczba].nazwa, s_pakiet_wpisy[i].nazwa, sizeof(s_inwentarz_biezacy[0].nazwa));
            tapety_kopiuj(s_inwentarz_biezacy[biezaca_liczba].wyswietlana, s_pakiet_wpisy[i].wyswietlana, sizeof(s_inwentarz_biezacy[0].wyswietlana));
            s_inwentarz_biezacy[biezaca_liczba].rozmiar = s_pakiet_wpisy[i].rozmiar;
            s_inwentarz_biezacy[biezaca_liczba].crc32 = s_pakiet_wpisy[i].crc32;
            ++biezaca_liczba;
            zainstalowane_tapety=static_cast<uint32_t>(biezaca_liczba);
        }
    }

    (void)zapisz_inwentarz(s_inwentarz_biezacy, biezaca_liczba);
    log_liczba("[TAPETA] Zainstalowano brakujacych: ", nowe);
    log_liczba("[TAPETA] Zachowano istniejacych: ", zachowane);

    // Krok 3: Zwolnienie pamieci fizycznej modulu tapety.pak
    if (!instalator_tryb()) {
        OdblokujZakres(mod.start, mod.rozmiar);
        klog_serial("[PMM] Pamiec fizyczna modulu tapety.pak zwolniona do puli PMM.");
    }
}

bool wczytaj(const char* path, BtpInfo* info, uint32_t** zrodlo, bool cicho, bool use_cache = false) {
    if (!bufory_gotowe || !info || !zrodlo || !tapety_sciezka(path)) return false;
    size_t cap = 0;
    uint8_t* scratch = grafika_scratch_tapety(&cap);
    // Retain the selected decoded source in the shared scratch arena. A mode
    // change can rebuild its scaled surface without filesystem I/O.
    static char cache_path[TAPETY_SCIEZKA_MAX]{};
    static BtpInfo cache_info{};
    static uint32_t* cache_pixels = nullptr;
    if (use_cache && cache_pixels && tapety_rowne(cache_path, path)) {
        *info = cache_info;
        *zrodlo = cache_pixels;
        return true;
    }
    const uint32_t rozmiar = rozmiar_pliku(path);
    cache_pixels = nullptr;
    cache_path[0] = 0;
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
    tapety_kopiuj(cache_path, path, sizeof(cache_path));
    cache_info = *info;
    cache_pixels = piksele;
    *zrodlo = piksele;
    return true;
}

bool zastosuj(const TapetyKonfiguracja& c, bool cicho, bool publikuj = true) {
    if (static_cast<uint32_t>(c.tryb) > 3) return false;
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
    aktualna.wyswietlana[0] = '\0';
    for (size_t i = 0; i < TAPETY_INWENTARZ_MAX; ++i) {
        char path[TAPETY_SCIEZKA_MAX]{};
        size_t p = 0;
        constexpr char prefix[] = "/tapety/";
        for (; prefix[p] && p + 1 < sizeof(path); ++p) path[p] = prefix[p];
        for (size_t k = 0; s_inwentarz_biezacy[i].nazwa[k] && p + 1 < sizeof(path); ++k)
            path[p++] = s_inwentarz_biezacy[i].nazwa[k];
        path[p] = '\0';
        if (tapety_rowne(path, c.sciezka)) {
            tapety_kopiuj(aktualna.wyswietlana, s_inwentarz_biezacy[i].wyswietlana, sizeof(aktualna.wyswietlana));
            break;
        }
    }
    jpeg_w = jpeg_h = 0;
    jpeg_do_przywrocenia.sciezka[0] = 0;
    if (publikuj) grafika_publikuj_tapete();
    if (!cicho) {
#if BURSZTYNDIAGTAPETYPERF
        log_sciezka("[TAPETA] Wybrano ", c.sciezka);
        log_sciezka("[TAPETA] Tryb ", tapety_nazwa_trybu(c.tryb));
#if BURSZTYN_DIAG_TAPETY
        log_liczba("[TAPETA] BTP szerokosc ", info.szerokosc);
        log_liczba("[TAPETA] BTP wysokosc ", info.wysokosc);
        klog_serial(info.kompresja == 2 ? "[TAPETA] BTP2 LZ/RLE" :
                    info.kompresja ? "[TAPETA] RLE" : "[TAPETA] RAW");
        log_liczba("[TAPETA] Odczyt us ", statystyki.odczyt_us);
        log_liczba("[TAPETA] Dekodowanie us ", statystyki.dekoduj_us);
        log_liczba("[TAPETA] Przygotowanie us ", statystyki.skaluj_us);
#endif
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
    out->adres_bufora = 0;
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
            if (tapety_koncowka(path, ".jpg") || tapety_koncowka(path, ".jpeg")) {
                if (rozmiar_pliku(path) >= 4) ++ok;
                else ++bledy;
                continue;
            }
            if (tapety_koncowka(path, ".png")) {
                if (rozmiar_pliku(path) >= 8) ++ok;
                else ++bledy;
                continue;
            }
            BtpInfo info{}; uint32_t* src = nullptr;
            if (wczytaj(path, &info, &src, true)) ++ok;
            else ++bledy;
        }
    } else if (grupa == 6) {
        const TapetyKonfiguracja stara = aktualna;
        size_t cap = 0;
        const uint32_t* adres = grafika_bufor_tapety(&cap);
        const size_t stare_bajty = cap;
        size_t btp_indeksy[TAPETY_INWENTARZ_MAX]{};
        size_t btp_liczba = 0;
        for (size_t i = 0; i < ile; ++i) {
            if (tapety_koncowka(s_inwentarz_wpisy[i].nazwa, ".btp")) {
                btp_indeksy[btp_liczba++] = i;
            }
        }
        przed = heap_zajete_bajty();
        for (uint32_t i = 0; i < 100; ++i) {
            TapetyKonfiguracja c = tapety_domyslna();
            if (btp_liczba > 0) {
                const auto& z = s_inwentarz_wpisy[btp_indeksy[i % btp_liczba]];
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
            grafika_tapeta_solid();
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

uint32_t tapety_liczba_zainstalowanych(){return zainstalowane_tapety;}
uint32_t tapety_liczba_wymaganych(){return wymagane_tapety;}

bool tapety_weryfikuj_instalacje(){
    const size_t n=wczytaj_inwentarz(s_inwentarz_wpisy,TAPETY_INWENTARZ_MAX);
    if(!n || (wymagane_tapety && n!=wymagane_tapety))return false;
    for(size_t i=0;i<n;++i){char path[TAPETY_SCIEZKA_MAX]{};size_t k=0;const char*prefix="/tapety/";
        while(prefix[k]){path[k]=prefix[k];++k;}
        for(size_t j=0;s_inwentarz_wpisy[i].nazwa[j]&&k+1<sizeof(path);++j)path[k++]=s_inwentarz_wpisy[i].nazwa[j];
        const auto&e=s_inwentarz_wpisy[i];
        if(rozmiar_pliku(path)!=e.rozmiar||tapety_plik_crc32(path,e.rozmiar)!=e.crc32)return false;
        char mini[TAPETY_SCIEZKA_MAX]{};
        if(!tapety_sciezka_miniatury_dla_tapety(e.nazwa,mini,sizeof(mini))||!tapety_miniatura_plik_poprawny(mini))return false;
    }
    return true;
}
size_t weryfikacja_tapet_indeks=0,weryfikacja_tapet_liczba=0;
uint32_t weryfikacja_tapet_offset=0,weryfikacja_tapet_crc=~0U;
uint64_t weryfikacja_tapet_bajty=0;
char weryfikacja_tapet_buf[4*1024];
bool tapety_weryfikuj_start(uint64_t*razem){
    weryfikacja_tapet_liczba=wczytaj_inwentarz(s_inwentarz_wpisy,TAPETY_INWENTARZ_MAX);
    if(!weryfikacja_tapet_liczba||(wymagane_tapety&&weryfikacja_tapet_liczba!=wymagane_tapety))return false;
    uint64_t total=0;for(size_t i=0;i<weryfikacja_tapet_liczba;++i){
        if(total>UINT64_MAX-s_inwentarz_wpisy[i].rozmiar)return false;
        total+=s_inwentarz_wpisy[i].rozmiar;
    }
    weryfikacja_tapet_indeks=0;weryfikacja_tapet_offset=0;weryfikacja_tapet_crc=~0U;weryfikacja_tapet_bajty=0;
    if(razem)*razem=total;
    return true;
}
bool tapety_weryfikuj_step(uint64_t*wykonane,bool*gotowe){
    if(wykonane)*wykonane=weryfikacja_tapet_bajty;
    if(gotowe)*gotowe=false;
    if(weryfikacja_tapet_indeks>=weryfikacja_tapet_liczba){if(gotowe)*gotowe=true;return true;}
    const auto&e=s_inwentarz_wpisy[weryfikacja_tapet_indeks];char path[TAPETY_SCIEZKA_MAX]{};size_t p=0;
    constexpr char prefix[]="/tapety/";for(;prefix[p]&&p+1<sizeof(path);++p){path[p]=prefix[p];}
    for(size_t i=0;e.nazwa[i]&&p+1<sizeof(path);++i){path[p++]=e.nazwa[i];}path[p]=0;
    if(weryfikacja_tapet_offset==0&&rozmiar_pliku(path)!=e.rozmiar)return false;
    uint32_t n=e.rozmiar-weryfikacja_tapet_offset;if(n>sizeof(weryfikacja_tapet_buf))n=sizeof(weryfikacja_tapet_buf);uint32_t got=0;
    if(n&&(!czytaj_fragment_pliku(path,weryfikacja_tapet_offset,weryfikacja_tapet_buf,n,&got)||got!=n))return false;
    for(uint32_t i=0;i<n;++i){weryfikacja_tapet_crc^=uint8_t(weryfikacja_tapet_buf[i]);for(unsigned bit=0;bit<8;++bit)weryfikacja_tapet_crc=(weryfikacja_tapet_crc>>1)^(0xEDB88320U&(0U-(weryfikacja_tapet_crc&1)));}
    weryfikacja_tapet_offset+=n;weryfikacja_tapet_bajty+=n;
    if(weryfikacja_tapet_offset==e.rozmiar){if(~weryfikacja_tapet_crc!=e.crc32)return false;++weryfikacja_tapet_indeks;weryfikacja_tapet_offset=0;weryfikacja_tapet_crc=~0U;}
    if(wykonane)*wykonane=weryfikacja_tapet_bajty;
    if(weryfikacja_tapet_indeks==weryfikacja_tapet_liczba&&gotowe)*gotowe=true;
    return true;
}
bool tapety_instaluj_start(uint64_t mbi,uint64_t*razem){
    instaluj(mbi,true);
    if(razem)*razem=instalacja_bajty_razem;
    return instalacja_przygotowana;
}
bool tapety_instaluj_step(uint64_t*wykonane,uint32_t*elementy,uint32_t*wszystkie,bool*gotowe){
    if(wykonane)*wykonane=instalacja_bajty;
    if(elementy)*elementy=static_cast<uint32_t>(instalacja_biezaca_liczba);
    if(wszystkie)*wszystkie=static_cast<uint32_t>(instalacja_pakiet_liczba);
    if(gotowe)*gotowe=false;
    if(!instalacja_przygotowana||instalacja_blad)return false;
    if(instalacja_indeks==instalacja_pakiet_liczba){
        if(!zapisz_inwentarz(s_inwentarz_biezacy,instalacja_biezaca_liczba)){instalacja_blad=true;return false;}
        instalacja_przygotowana=false;if(gotowe)*gotowe=true;return true;
    }
    const auto&e=s_pakiet_wpisy[instalacja_indeks];char path[TAPETY_SCIEZKA_MAX]{};
    if(!tapety_sciezka_z_pakietu(e,path,sizeof(path))){instalacja_blad=true;return false;}
    constexpr uint32_t PORCJA=4*1024;
    static_assert(PORCJA<=4096,"WALLPAPERS STEP nie moze przekroczyc jednego bloku BSP2");
    if(instalacja_crc_offset<e.rozmiar){
        uint32_t n=e.rozmiar-instalacja_crc_offset;if(n>PORCJA)n=PORCJA;
        for(uint32_t i=0;i<n;++i){
            instalacja_crc^=e.payload[instalacja_crc_offset+i];
            for(unsigned bit=0;bit<8;++bit)instalacja_crc=(instalacja_crc>>1)^(0xEDB88320U&(0U-(instalacja_crc&1)));
        }
        instalacja_crc_offset+=n;
        if(instalacja_crc_offset==e.rozmiar&&~instalacja_crc!=e.crc32){instalacja_blad=true;return false;}
        return true;
    }
    bool zachowany=false;
    if(instalacja_offset==0){
        psf_metadane meta{};
        zachowany=pobierz_metadane_tworu(path,&meta)&&meta.rozmiar==e.rozmiar;
        if(!zachowany){
            (void)usun_twor(path);
            if(!utworz_plik(path)){instalacja_blad=true;return false;}
        }
    }
    uint32_t n=zachowany?e.rozmiar:e.rozmiar-instalacja_offset;if(n>PORCJA&&!zachowany)n=PORCJA;
    if(!zachowany&&!zapisz_fragment_pliku(path,instalacja_offset,reinterpret_cast<const char*>(e.payload+instalacja_offset),n)){
        (void)usun_twor(path);instalacja_blad=true;return false;
    }
    if(!zachowany&&!psf_flush_cache()){
        (void)usun_twor(path);instalacja_blad=true;return false;
    }
    instalacja_offset+=n;instalacja_bajty+=n;
    if(instalacja_offset==e.rozmiar){
        if(!psf_flush_cache()){instalacja_blad=true;return false;}
        if(!e.miniatura&&instalacja_biezaca_liczba<TAPETY_INWENTARZ_MAX){
            s_inwentarz_biezacy[instalacja_biezaca_liczba]={};
            tapety_kopiuj(s_inwentarz_biezacy[instalacja_biezaca_liczba].nazwa,e.nazwa,sizeof(s_inwentarz_biezacy[0].nazwa));
            tapety_kopiuj(s_inwentarz_biezacy[instalacja_biezaca_liczba].wyswietlana,e.wyswietlana,sizeof(s_inwentarz_biezacy[0].wyswietlana));
            s_inwentarz_biezacy[instalacja_biezaca_liczba].rozmiar=e.rozmiar;
            s_inwentarz_biezacy[instalacja_biezaca_liczba].crc32=e.crc32;
            ++instalacja_biezaca_liczba;zainstalowane_tapety=static_cast<uint32_t>(instalacja_biezaca_liczba);
        }
        ++instalacja_indeks;instalacja_offset=0;instalacja_crc_offset=0;instalacja_crc=~0U;
    }
    if(wykonane)*wykonane=instalacja_bajty;
    if(elementy)*elementy=static_cast<uint32_t>(instalacja_biezaca_liczba);
    return true;
}
bool tapety_instaluj_sprawdz(uint64_t mbi){instaluj(mbi);return tapety_weryfikuj_instalacje();}

static bool czy_plik_tapety_poprawny(const char* path) {
    if (!path || !path[0]) return false;
    psf_metadane meta{};
    if (!pobierz_metadane_tworu(path, &meta) || meta.rozmiar < 4 || meta.rozmiar > TAPETY_PLIK_MAX) {
        return false;
    }
    if (tapety_koncowka(path, ".btp")) {
        if (meta.rozmiar < 14) return false;
        char mag[4]{};
        uint32_t got = 0;
        if (!czytaj_fragment_pliku(path, 0, mag, 4, &got) || got != 4) return false;
        if ((mag[0] == 'B' && mag[1] == 'T' && mag[2] == 'P' && mag[3] == '1') ||
            (mag[0] == 'B' && mag[1] == 'T' && mag[2] == 'P' && mag[3] == '2')) {
            return true;
        }
        return false;
    }
    if (tapety_koncowka(path, ".jpg") || tapety_koncowka(path, ".jpeg")) {
        char mag[2]{};
        uint32_t got = 0;
        if (!czytaj_fragment_pliku(path, 0, mag, 2, &got) || got != 2) return false;
        if (static_cast<uint8_t>(mag[0]) == 0xFF && static_cast<uint8_t>(mag[1]) == 0xD8) {
            return true;
        }
        return false;
    }
    if (tapety_koncowka(path, ".png")) {
        char mag[8]{};
        uint32_t got = 0;
        if (!czytaj_fragment_pliku(path, 0, mag, 8, &got) || got != 8) return false;
        if (static_cast<uint8_t>(mag[0]) == 0x89 && mag[1] == 'P' && mag[2] == 'N' && mag[3] == 'G' &&
            mag[4] == 0x0D && mag[5] == 0x0A && mag[6] == 0x1A && mag[7] == 0x0A) {
            return true;
        }
        return false;
    }
    return false;
}

static TapetyZrodlo zastosuj_lub_zaplanuj(const TapetyKonfiguracja& c) {
    if (!czy_plik_tapety_poprawny(c.sciezka)) return TAPETY_SOLID;
    if (tapety_koncowka(c.sciezka, ".btp")) {
        if (zastosuj(c, false)) {
            return TAPETY_BTP;
        }
        return TAPETY_SOLID;
    }
    if (tapety_koncowka(c.sciezka, ".jpg") || tapety_koncowka(c.sciezka, ".jpeg") || tapety_koncowka(c.sciezka, ".png")) {
        grafika_tapeta_solid();
        aktualna = c;
        if (!aktualna.wyswietlana[0]) {
            for (size_t i = 0; i < TAPETY_INWENTARZ_MAX; ++i) {
                char path[TAPETY_SCIEZKA_MAX]{};
                size_t p = 0;
                constexpr char prefix[] = "/tapety/";
                for (; prefix[p] && p + 1 < sizeof(path); ++p) path[p] = prefix[p];
                for (size_t k = 0; s_inwentarz_wpisy[i].nazwa[k] && p + 1 < sizeof(path); ++k)
                    path[p++] = s_inwentarz_wpisy[i].nazwa[k];
                path[p] = '\0';
                if (tapety_rowne(path, c.sciezka)) {
                    tapety_kopiuj(aktualna.wyswietlana, s_inwentarz_wpisy[i].wyswietlana, sizeof(aktualna.wyswietlana));
                    break;
                }
            }
        }
        jpeg_do_przywrocenia = aktualna;
        const bool is_png = tapety_koncowka(c.sciezka, ".png");
        statystyki.zrodlo = is_png ? TAPETY_PNG : TAPETY_JPEG;
#if BURSZTYNDIAGTAPETYPERF
        log_sciezka(is_png ? "[TAPETA] Wybrano (PNG) " : "[TAPETA] Wybrano (JPEG) ", c.sciezka);
#endif
        return is_png ? TAPETY_PNG : TAPETY_JPEG;
    }
    return TAPETY_SOLID;
}

void tapety_inicjalizuj(uint64_t mbi) {
    if (mbi == 0) mbi = g_multiboot_info_ptr;
    aktualna = {};
    jpeg_do_przywrocenia = {};
    instaluj(mbi);
    bufory_gotowe = grafika_przygotuj_bufory_tapety();
    if (!bufory_gotowe) klog_serial("[TAPETA] BLAD buforow BTP; uzywam fallback.");

    // Krok 1: Sprawdz zapisana konfiguracje (persisted wallpaper configuration)
    TapetyKonfiguracja zapisana{};
    bool ma_zapisana = false;
    const uint32_t n = rozmiar_pliku(TAPETY_CFG);
    if (n && n <= TAPETY_CFG_MAX) {
        char cfg[TAPETY_CFG_MAX]{};
        if (czytaj_dokladnie(TAPETY_CFG, cfg, n) && tapety_parsuj(cfg, n, &zapisana)) {
            ma_zapisana = true;
        }
    }

    // Wczytaj inwentarz i zadeklarowana domyslna z manifestu
    for (size_t i = 0; i < TAPETY_INWENTARZ_MAX; ++i) s_inwentarz_wpisy[i] = {};
    char manifest_domyslna[TAPETY_SCIEZKA_MAX]{};
    const size_t ile_manifest = wczytaj_inwentarz(s_inwentarz_wpisy, TAPETY_INWENTARZ_MAX,
                                                  manifest_domyslna, sizeof(manifest_domyslna));

    statystyki.zrodlo = tapety_fallback(
        // 1. Zapisana konfiguracja
        [&]() -> TapetyZrodlo {
            if (ma_zapisana) {
                if (czy_plik_tapety_poprawny(zapisana.sciezka)) {
                    TapetyZrodlo z = zastosuj_lub_zaplanuj(zapisana);
                    if (z != TAPETY_SOLID) return z;
                }
                klog_serial("[TAPETA] Zapisana tapeta nie istnieje; wybieram domyslna z manifestu.");
            }
            return TAPETY_SOLID;
        },
        // 2. Domyslna tapeta z manifestu
        [&]() -> TapetyZrodlo {
            if (manifest_domyslna[0]) {
                TapetyKonfiguracja def_cfg{};
                char path[TAPETY_SCIEZKA_MAX]{};
                size_t p = 0;
                constexpr char prefix[] = "/tapety/";
                for (; prefix[p] && p + 1 < sizeof(path); ++p) path[p] = prefix[p];
                for (size_t k = 0; manifest_domyslna[k] && p + 1 < sizeof(path); ++k) path[p++] = manifest_domyslna[k];
                path[p] = '\0';
                tapety_kopiuj(def_cfg.sciezka, path, sizeof(def_cfg.sciezka));
                def_cfg.tryb = TrybSkalowaniaObrazu::Wypelnij;
                if (czy_plik_tapety_poprawny(def_cfg.sciezka)) {
                    TapetyZrodlo z = zastosuj_lub_zaplanuj(def_cfg);
                    if (z != TAPETY_SOLID) return z;
                }
            }
            if (ile_manifest > 0) {
                klog_serial("[TAPETA] Domyslna tapeta z manifestu jest niedostepna; wybieram pierwsza poprawna.");
            }
            return TAPETY_SOLID;
        },
        // 3. Pierwszy poprawny wpis z manifestu
        [&]() -> TapetyZrodlo {
            for (size_t i = 0; i < ile_manifest; ++i) {
                if (!s_inwentarz_wpisy[i].nazwa[0]) continue;
                TapetyKonfiguracja alt{};
                char path[TAPETY_SCIEZKA_MAX]{};
                size_t p = 0;
                constexpr char prefix[] = "/tapety/";
                for (; prefix[p] && p + 1 < sizeof(path); ++p) path[p] = prefix[p];
                for (size_t k = 0; s_inwentarz_wpisy[i].nazwa[k] && p + 1 < sizeof(path); ++k) path[p++] = s_inwentarz_wpisy[i].nazwa[k];
                path[p] = '\0';
                tapety_kopiuj(alt.sciezka, path, sizeof(alt.sciezka));
                alt.tryb = TrybSkalowaniaObrazu::Wypelnij;
                tapety_kopiuj(alt.wyswietlana, s_inwentarz_wpisy[i].wyswietlana, sizeof(alt.wyswietlana));
                if (czy_plik_tapety_poprawny(alt.sciezka)) {
                    TapetyZrodlo z = zastosuj_lub_zaplanuj(alt);
                    if (z != TAPETY_SOLID) return z;
                }
            }
            return TAPETY_SOLID;
        },
        // 4. Awaryjny jednolity kolor
        [&]() {
            klog_serial("[TAPETA] Brak poprawnej tapety w manifescie; uzywam awaryjnego jednolitego koloru.");
            grafika_tapeta_solid();
            aktualna = {};
            jpeg_do_przywrocenia = {};
        }
    );

    if (statystyki.zrodlo == TAPETY_SOLID) {
        aktualna.sciezka[0] = 0;
    }
    TapetyOdpowiedz s{}; snapshot(&s);
#if BURSZTYN_DIAG_TAPETY
    log_liczba("[TAPETA] Bufor aktywny bajty ", s.bufor_bajty);
    log_liczba("[TAPETA] Scratch bufor bajty ", s.scratch_bajty);
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
    if (req.operacja == TAPETY_STAN) {
        if (jpeg_do_przywrocenia.sciezka[0]) {
            out->tryb = static_cast<uint32_t>(jpeg_do_przywrocenia.tryb);
            tapety_kopiuj(out->sciezka, jpeg_do_przywrocenia.sciezka, sizeof(out->sciezka));
        }
        out->status = TAPETY_OK; return true;
    }
    if (req.operacja == TAPETY_DIAGNOSTYKA && test && (req.grupa == 3 || req.grupa == 6)) {
        diagnostyka(req.grupa, out);
        return true;
    }
    if (req.operacja != TAPETY_ZASTOSUJ || req.grupa != 0 || req.tryb > 3 || !tapety_sciezka(req.sciezka)) return false;
    TapetyKonfiguracja c{};
    tapety_kopiuj(c.sciezka, req.sciezka, sizeof(c.sciezka));
    c.tryb = static_cast<TrybSkalowaniaObrazu>(req.tryb);
    if (!zastosuj(c, false)) return true;
    const bool zapis = zapisz_wybor();
    snapshot(out);
    out->status = zapis ? TAPETY_OK : TAPETY_BLAD_ZAPISU;
    if (zapis) {
#if BURSZTYNDIAGTAPETYPERF
        klog_serial("[TAPETA] Wybor zapisany w BSP2.");
#endif
    } else {
        klog_serial("[TAPETA] BLAD zapisu wyboru; tapeta aktywna tylko w tej sesji.");
    }
    return true;
}

bool tapety_czy_jpeg_do_przywrocenia() { return jpeg_do_przywrocenia.sciezka[0] != 0; }

// Surface submission: the JPEG file and its entropy stream are never parsed in Ring 0.
bool tapety_obsluz_obraz(const proces_t& proces, const TapetyObrazZadanie& req, TapetyOdpowiedz* out) {
    const TapetyZadanie& base = req.podstawowe;
    if (!out || base.wersja != 2 || base.operacja != TAPETY_ZASTOSUJ_OBRAZ || base.grupa != 0 ||
        base.tryb > 3 || !tapety_sciezka_powierzchni(base.sciezka) ||
        !tapety_rowne(proces.sciezka_pliku, TAPETY_PROGRAM, sizeof(proces.sciezka_pliku)) ||
        !pzb_ma_co_najmniej_poziom(&proces, PZB_UZYTKOWNIK) ||
        !pzb_ma_prawa(&proces, PRAWO_GUI | PRAWO_PLIKI_CZYTAJ)) return false;
    snapshot(out);
    out->status = TAPETY_BLAD;
    const uint64_t w = req.szerokosc, h = req.wysokosc;
    uint64_t pixels = 0, bytes = 0, stride = 0;
    if (!w || !h || w > 8192 || h > 8192 ||
        __builtin_mul_overflow(w, h, &pixels) ||
        __builtin_mul_overflow(pixels, 4ULL, &bytes) ||
        __builtin_mul_overflow(w, 4ULL, &stride) ||
        pixels > TAPETY_PIKSELE_MAX * 2ULL ||
        req.format_pikseli != 1 || req.stride != stride || req.bajty != bytes ||
        req.bajty > 32ULL * 1024 * 1024 || !req.piksele || !rozmiar_pliku(base.sciezka)) return true;
    const void* user = reinterpret_cast<const void*>(req.piksele);
    if (!czy_bezpieczny_zakres_uzytkownika_do_odczytu(user, static_cast<size_t>(req.bajty))) return true;
    size_t scratch_cap = 0, dst_cap = 0;
    uint8_t* scratch = grafika_scratch_tapety(&scratch_cap);
    uint32_t* dst = grafika_bufor_tapety(&dst_cap);
    const int screen_w = grafika_pobierz_szerokosc(), screen_h = grafika_pobierz_wysokosc();
    GeometriaObrazu geom{};
    if (!scratch || !dst || req.bajty > scratch_cap || screen_w <= 0 || screen_h <= 0 ||
        !geometria_obrazu(req.szerokosc, req.wysokosc, static_cast<uint32_t>(screen_w),
                          static_cast<uint32_t>(screen_h), static_cast<TrybSkalowaniaObrazu>(base.tryb), &geom) ||
        static_cast<uint64_t>(screen_w) * screen_h * 4 > dst_cap) return true;
    [[maybe_unused]] const uint64_t apply_start = czas_monotoniczny_us();
    uint8_t* stage = static_cast<uint8_t*>(bufor_grafiki_alokuj(static_cast<size_t>(req.bajty)));
    if (!stage) return true;
    const bool copied = skopiuj_z_przestrzeni_uzytkownika(stage, user, static_cast<size_t>(req.bajty));
    bool opaque = copied;
    if (opaque) for (size_t i = 3; i < req.bajty; i += 4) if (stage[i] != 255) { opaque = false; break; }
    if (!opaque) { bufor_grafiki_zwolnij(stage); return true; }
    for (size_t i = 0; i < req.bajty; ++i) scratch[i] = stage[i];
    bufor_grafiki_zwolnij(stage);
    const uint64_t t = czas_monotoniczny_us();
    if (!skaluj_obraz(reinterpret_cast<const uint32_t*>(scratch), req.szerokosc, req.wysokosc,
        static_cast<size_t>(req.bajty), dst, static_cast<uint32_t>(screen_w), static_cast<uint32_t>(screen_h),
        static_cast<uint32_t>(screen_w), dst_cap, static_cast<TrybSkalowaniaObrazu>(base.tryb), TAPETY_TLO)) return true;
    statystyki.skaluj_us = czas_monotoniczny_us() - t;
    jpeg_w = req.szerokosc; jpeg_h = req.wysokosc;
    aktualna = {};
    tapety_kopiuj(aktualna.sciezka, base.sciezka, sizeof(aktualna.sciezka));
    aktualna.tryb = static_cast<TrybSkalowaniaObrazu>(base.tryb);
    for (size_t i = 0; i < TAPETY_INWENTARZ_MAX; ++i) {
        char path[TAPETY_SCIEZKA_MAX]{};
        size_t p = 0;
        constexpr char prefix[] = "/tapety/";
        for (; prefix[p] && p + 1 < sizeof(path); ++p) path[p] = prefix[p];
        for (size_t k = 0; s_inwentarz_biezacy[i].nazwa[k] && p + 1 < sizeof(path); ++k)
            path[p++] = s_inwentarz_biezacy[i].nazwa[k];
        path[p] = '\0';
        if (tapety_rowne(path, base.sciezka)) {
            tapety_kopiuj(aktualna.wyswietlana, s_inwentarz_biezacy[i].wyswietlana, sizeof(aktualna.wyswietlana));
            break;
        }
    }
    jpeg_do_przywrocenia.sciezka[0] = 0;
    statystyki.zrodlo = tapety_sciezka_png(base.sciezka) ? TAPETY_PNG :
                        tapety_sciezka_jpeg(base.sciezka) ? TAPETY_JPEG : TAPETY_BTP;
    ++statystyki.zmiany;
    ++statystyki.przygotowania;
    grafika_publikuj_tapete();
    const bool saved = zapisz_wybor();
    snapshot(out);
    out->status = saved ? TAPETY_OK : TAPETY_BLAD_ZAPISU;
#if BURSZTYNDIAGTAPETYPERF
    const char* zrodlo_tag = tapety_sciezka_png(base.sciezka) ? "[TAPETA] PNG wybrano " :
                             tapety_sciezka_jpeg(base.sciezka) ? "[TAPETA] JPEG wybrano " :
                             "[TAPETA] BTP wybrano ";
    log_sciezka(zrodlo_tag, base.sciezka);
    const char* us_tag = tapety_sciezka_png(base.sciezka) ? "[TAPETA] PNG zastosowanie us " :
                         tapety_sciezka_jpeg(base.sciezka) ? "[TAPETA] JPEG zastosowanie us " :
                         "[TAPETA] BTP zastosowanie us ";
    log_liczba(us_tag, czas_monotoniczny_us() - apply_start);
#endif
    klog_serial("[TAPETA] Tapeta gotowa.");
    if (!saved) {
        klog_serial("[TAPETA] BLAD zapisu wyboru; tapeta aktywna tylko w tej sesji.");
    }
    return true;
}

bool tapety_przygotuj_ekran(uint32_t* dst,uint32_t w,uint32_t h,size_t cap) {
    if(!dst||!w||!h||uint64_t(w)*h*4>cap)return false;
    if(!grafika_czy_tapeta()) {
        for(size_t i=0;i<size_t(w)*h;++i)dst[i]=TAPETY_TLO;
        return true;
    }
    if (jpeg_w && jpeg_h && tapety_sciezka_powierzchni(aktualna.sciezka)) {
        size_t source_cap = 0;
        auto* source = reinterpret_cast<const uint32_t*>(grafika_scratch_tapety(&source_cap));
        if (!source || static_cast<uint64_t>(jpeg_w) * jpeg_h * 4 > source_cap) return false;
        return skaluj_obraz(source, jpeg_w, jpeg_h, source_cap, dst, w, h, w, cap, aktualna.tryb, TAPETY_TLO);
    }
    BtpInfo info{};uint32_t* src=nullptr;
    if(!wczytaj(aktualna.sciezka,&info,&src,true,true))return false;
    if(!skaluj_obraz(src,info.szerokosc,info.wysokosc,size_t(info.szerokosc)*info.wysokosc*4,
        dst,w,h,w,cap,aktualna.tryb,TAPETY_TLO))return false;
    ++statystyki.przygotowania;
    return true;
}
