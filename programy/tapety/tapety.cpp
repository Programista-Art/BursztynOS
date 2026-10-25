/* Tapety: pojedyncza instancja, miniatury i zatwierdzanie przez IPC. */
#include "../../loader.h"
#include "../../bursztyn_ui/bursztyn_ui.h"

using namespace BursztynUI;

extern "C" void _start();
static constexpr uint64_t BUR_TEKST_PRZESUNIECIE = 0x1000ULL;
static constexpr uint64_t BUR_TEKST_ROZMIAR = 0x44000ULL;
static constexpr uint64_t BUR_TEKST_WIRTUALNY = 0x1001000ULL;
static constexpr uint64_t BUR_DANE_PRZESUNIECIE = 0x45000ULL;
static constexpr uint64_t BUR_DANE_ROZMIAR = 0x20000ULL;
static constexpr uint64_t BUR_DANE_WIRTUALNY = 0x1045000ULL;

extern "C" {
__attribute__((section(".naglowek"), used)) NaglowekBur naglowek = {
    {'B','U','R','\0'}, (uint64_t)&_start,
    BUR_TEKST_PRZESUNIECIE, BUR_TEKST_ROZMIAR, BUR_TEKST_WIRTUALNY,
    BUR_DANE_PRZESUNIECIE, BUR_DANE_ROZMIAR, BUR_DANE_WIRTUALNY
};
}
static_assert(sizeof(NaglowekBur) == 60U, "Naglowek .bur musi miec 60 bajtow");

#include "klient.h"
#include "miniatury.h"
#include "../../obrazy/dekoder_obrazu.h"

namespace {
#ifndef BURSZTYNDIAGTAPETYPERF
#define BURSZTYNDIAGTAPETYPERF 0
#endif

constexpr uint32_t TAPETY_MAX = 65;
constexpr size_t KATALOG_BUFOR_MAX = 16384;

struct ElementTapety {
    char sciezka[TAPETY_SCIEZKA_MAX];
    char nazwa[TAPETY_SCIEZKA_MAX];
};

ElementTapety lista_tapet[TAPETY_MAX];
int liczba_tapet = 0;

void log(const char* t) { bws_wywolaj(1, reinterpret_cast<uint64_t>(t), 0, 0, 0); }
uint64_t endpoint;
WidokMiniatur* galeria;
Etykieta* komunikat;
MiniaturyTapet* miniatury = nullptr;
uint32_t tryb = 1;
int wybrana = 0;
uint64_t start_ns = 0;
uint32_t miniatury_btp_wczytane = 0;
uint32_t miniatury_fallback_pelny = 0;
uint64_t miniatury_pamiec_bajty = 0;

[[maybe_unused]] void log_perf_uint64(uint64_t v) {
    char buf[24];
    int pos = 0;
    if (v == 0) {
        buf[pos++] = '0';
    } else {
        char rev[24];
        int rpos = 0;
        while (v > 0 && rpos < static_cast<int>(sizeof(rev))) {
            rev[rpos++] = static_cast<char>('0' + (v % 10));
            v /= 10;
        }
        while (rpos > 0 && pos + 1 < static_cast<int>(sizeof(buf))) buf[pos++] = rev[--rpos];
    }
    buf[pos] = '\0';
    log(buf);
}

void log_blad_obrazu(const char* opis, const char* path) {
    log("[TAPETY APP] BLAD ");
    log(opis ? opis : "obrazu");
    log(" ");
    log(path ? path : "(nieznany)");
    log("\n");
}

void log_perf(const char* opis, uint64_t us) {
#if BURSZTYNDIAGTAPETYPERF
    log("[TAPETY PERF] ");
    log(opis);
    log(" ");
    log_perf_uint64(us);
    log(" us\n");
#else
    (void)opis;
    (void)us;
#endif
}

bool wczytaj_plik_obrazu(const char* path, DekodowanyObraz* out) {
    if (!out || !tapety_sciezka_powierzchni(path)) return false;
    uint32_t n = 0, read = 0;
    if (!pobierz_rozmiar_pliku(path, &n) || n < 4 || n > OBRAZ_MAX_PLIK) {
        log_blad_obrazu("rozmiaru/formatu", path);
        return false;
    }
    auto* file = static_cast<uint8_t*>(gui_malloc(n));
    if (!file) {
        log_blad_obrazu("alokacji pliku", path);
        return false;
    }
    const uint64_t start = gui_czas_monotoniczny_ns();
    const bool read_ok = czytaj_fragment_pliku(path, 0, reinterpret_cast<char*>(file), n, &read) && read == n;
    BladObrazu blad = BladObrazu::OK;
    bool ok = read_ok &&
        dekoduj_obraz(file, n, out,
            [](size_t bytes) -> void* { return gui_malloc(bytes); },
            [](void* ptr) { gui_free(ptr); }, &blad);
    gui_free(file);
    if (ok) {
        log_perf(tapety_sciezka_png(path) ? "PNG odczyt+decode" :
                 tapety_sciezka_jpeg(path) ? "JPEG odczyt+decode" :
                 "BTP odczyt+decode",
                 (gui_czas_monotoniczny_ns() - start) / 1000U);
    } else if (!read_ok) {
        log_blad_obrazu("odczytu pliku", path);
    } else {
        log_blad_obrazu(blad_obrazu_komunikat(blad), path);
    }
    return ok;
}

bool zastosuj_obraz(const char* path) {
    DekodowanyObraz image{};
    if (!wczytaj_plik_obrazu(path, &image)) return false;
    TapetyOdpowiedz out{};
    const bool ok = tapety_zastosuj_obraz(endpoint, path, tryb, image.piksele,
        image.szerokosc, image.wysokosc, image.stride, image.bajty, &out);
    gui_free(image.piksele);
    if (!ok || out.status == TAPETY_BLAD) return false;
    if (komunikat) komunikat->ustaw_tekst(out.status == TAPETY_OK ?
        "Tapeta zastosowana i zapisana." :
        "Tapeta aktywna. Nie udało się zapisać wyboru na dysku.");
    return true;
}

int porownaj_tekst(const char* a, const char* b) {
    if (!a || !b) return a ? 1 : (b ? -1 : 0);
    while (*a && *a == *b) { ++a; ++b; }
    return static_cast<int>(static_cast<uint8_t>(*a)) - static_cast<int>(static_cast<uint8_t>(*b));
}

void pobierz_nazwe_wyswietlana(const char* sciezka, const char* plik, char* dst, size_t cap) {
    if (!dst || !cap) return;
    dst[0] = '\0';
    for (size_t i = 0; i < TAPETY_LICZBA; ++i) {
        if (tapety_rowne(sciezka, TAPETY_SCIEZKI[i])) {
            tapety_kopiuj(dst, TAPETY_NAZWY[i], cap);
            return;
        }
    }
    if (plik && plik[0]) {
        tapety_kopiuj(dst, plik, cap);
        return;
    }
    size_t len = 0;
    while (plik && plik[len]) ++len;
    if (len >= 4 && tapety_koncowka(plik, ".btp")) {
        len -= 4;
    } else if (len >= 4 && tapety_koncowka(plik, ".jpg")) {
        len -= 4;
    } else if (len >= 4 && tapety_koncowka(plik, ".png")) {
        len -= 4;
    } else if (len >= 5 && tapety_koncowka(plik, ".jpeg")) {
        len -= 5;
    }
    size_t i = 0;
    for (; i < len && i + 1 < cap; ++i) dst[i] = plik[i];
    dst[i] = '\0';
}

void sortuj_tapety() {
    for (int i = 0; i < liczba_tapet - 1; ++i) {
        for (int j = 0; j < liczba_tapet - 1 - i; ++j) {
            if (porownaj_tekst(lista_tapet[j].sciezka, lista_tapet[j + 1].sciezka) > 0) {
                ElementTapety tmp = lista_tapet[j];
                lista_tapet[j] = lista_tapet[j + 1];
                lista_tapet[j + 1] = tmp;
            }
        }
    }
}

void odkryj_tapety() {
    liczba_tapet = 0;

    // 1. Wczytaj metadane z manifestu /tapety/manifest.cfg
    uint32_t man_sz = 0;
    if (pobierz_rozmiar_pliku("/tapety/manifest.cfg", &man_sz) && man_sz > 0 && man_sz < 65536) {
        char* mbuf = static_cast<char*>(gui_malloc(man_sz + 1));
        if (mbuf) {
            uint32_t read = 0;
            if (czytaj_fragment_pliku("/tapety/manifest.cfg", 0, mbuf, man_sz, &read) && read == man_sz) {
                mbuf[man_sz] = '\0';
                size_t pos = 0;
                while (pos < man_sz && liczba_tapet < static_cast<int>(TAPETY_MAX - 1)) {
                    size_t end = pos;
                    while (end < man_sz && mbuf[end] != '\n') ++end;
                    constexpr char prefix[] = "wpis=";
                    constexpr size_t plen = sizeof(prefix) - 1;
                    if (end - pos > plen) {
                        bool match = true;
                        for (size_t k = 0; k < plen; ++k) {
                            if (mbuf[pos + k] != prefix[k]) { match = false; break; }
                        }
                        if (match) {
                            size_t cur = pos + plen;
                            // 1. kanoniczna
                            size_t p1 = cur;
                            while (p1 < end && mbuf[p1] != '|') ++p1;
                            if (p1 < end) {
                                size_t klen = p1 - cur;
                                // 2. rozmiar
                                size_t p2 = p1 + 1;
                                size_t p3 = p2;
                                while (p3 < end && mbuf[p3] != '|') ++p3;
                                if (p3 < end) {
                                    // 3. crc32
                                    size_t p4 = p3 + 1;
                                    size_t p5 = p4;
                                    while (p5 < end && mbuf[p5] != '|') ++p5;

                                    char kanon[TAPETY_SCIEZKA_MAX]{};
                                    for (size_t k = 0; k < klen && k + 1 < sizeof(kanon); ++k) kanon[k] = mbuf[cur + k];
                                    kanon[klen] = '\0';

                                    char wyswietlana[TAPETY_SCIEZKA_MAX]{};
                                    size_t wlen = 0;
                                    if (p5 < end && mbuf[p5] == '|') {
                                        size_t w_start = p5 + 1;
                                        size_t w_end = end;
                                        if (w_end > w_start && mbuf[w_end - 1] == '\r') --w_end;
                                        wlen = w_end - w_start;
                                        if (wlen >= sizeof(wyswietlana)) wlen = sizeof(wyswietlana) - 1;
                                        for (size_t k = 0; k < wlen; ++k) wyswietlana[k] = mbuf[w_start + k];
                                        wyswietlana[wlen] = '\0';
                                    }
                                    if (wlen == 0) {
                                        tapety_kopiuj(wyswietlana, kanon, sizeof(wyswietlana));
                                    }

                                    char pelna[TAPETY_SCIEZKA_MAX]{};
                                    size_t sp = 0;
                                    constexpr char t_pref[] = "/tapety/";
                                    for (; t_pref[sp] && sp + 1 < sizeof(pelna); ++sp) pelna[sp] = t_pref[sp];
                                    for (size_t k = 0; kanon[k] && sp + 1 < sizeof(pelna); ++k) pelna[sp++] = kanon[k];
                                    pelna[sp] = '\0';

                                    uint32_t fsz = 0;
                                    if (pobierz_rozmiar_pliku(pelna, &fsz) && fsz >= 4) {
                                        tapety_kopiuj(lista_tapet[liczba_tapet].sciezka, pelna, sizeof(lista_tapet[0].sciezka));
                                        pobierz_nazwe_wyswietlana(pelna, wyswietlana, lista_tapet[liczba_tapet].nazwa, sizeof(lista_tapet[0].nazwa));
                                        ++liczba_tapet;
                                    }
                                }
                            }
                        }
                    }
                    pos = end + 1;
                }
            }
            gui_free(mbuf);
        }
    }

    // 2. Dodatkowe pliki w katalogu /tapety (nieujete w manifescie)
    char* bufor = static_cast<char*>(gui_malloc(KATALOG_BUFOR_MAX));
    if (!bufor) {
        sortuj_tapety();
        return;
    }
    for (size_t i = 0; i < KATALOG_BUFOR_MAX; ++i) bufor[i] = 0;

    if (!wylistuj_katalog_uzytkownika("/tapety", bufor, KATALOG_BUFOR_MAX)) {
        gui_free(bufor);
        sortuj_tapety();
        return;
    }

    size_t pos = 0;
    while (pos < KATALOG_BUFOR_MAX && bufor[pos]) {
        size_t end = pos;
        while (end < KATALOG_BUFOR_MAX && bufor[end] && bufor[end] != '\n') ++end;
        if (end <= pos) break;

        if (end - pos > 7 && bufor[pos] == '[' && bufor[pos + 1] == 'P' && bufor[pos + 2] == 'L' &&
            bufor[pos + 3] == 'I' && bufor[pos + 4] == 'K' && bufor[pos + 5] == ']' && bufor[pos + 6] == ' ') {

            const char* nazwa_pliku = bufor + pos + 7;
            size_t nazwa_len = end - (pos + 7);
            if (nazwa_len > 0 && nazwa_pliku[nazwa_len - 1] == '\r') --nazwa_len;

            if (nazwa_len >= 5 && nazwa_len < TAPETY_SCIEZKA_MAX - 10) {
                char plik[TAPETY_SCIEZKA_MAX]{};
                for (size_t i = 0; i < nazwa_len; ++i) plik[i] = nazwa_pliku[i];
                plik[nazwa_len] = '\0';

                const bool is_btp = tapety_koncowka(plik, ".btp");
                const bool is_jpeg = tapety_koncowka(plik, ".jpg") || tapety_koncowka(plik, ".jpeg");
                const bool is_png = tapety_koncowka(plik, ".png");

                if (is_btp || is_jpeg || is_png) {
                    char pelna[TAPETY_SCIEZKA_MAX]{};
                    size_t p = 0;
                    constexpr char prefix[] = "/tapety/";
                    for (; prefix[p] && p + 1 < sizeof(pelna); ++p) pelna[p] = prefix[p];
                    for (size_t i = 0; plik[i] && p + 1 < sizeof(pelna); ++i) pelna[p++] = plik[i];
                    pelna[p] = '\0';

                    bool already = false;
                    for (int k = 0; k < liczba_tapet; ++k) {
                        if (tapety_rowne(lista_tapet[k].sciezka, pelna)) {
                            already = true;
                            break;
                        }
                    }

                    if (!already) {
                        if (is_btp && tapety_sciezka(pelna)) {
                            uint32_t rozmiar = 0;
                            if (pobierz_rozmiar_pliku(pelna, &rozmiar) && rozmiar >= 14 && rozmiar <= TAPETY_PLIK_MAX) {
                                if (liczba_tapet < static_cast<int>(TAPETY_MAX - 1)) {
                                    tapety_kopiuj(lista_tapet[liczba_tapet].sciezka, pelna, sizeof(lista_tapet[0].sciezka));
                                    pobierz_nazwe_wyswietlana(pelna, plik, lista_tapet[liczba_tapet].nazwa, sizeof(lista_tapet[0].nazwa));
                                    ++liczba_tapet;
                                }
                            }
                        } else if (is_jpeg && tapety_sciezka_jpeg(pelna)) {
                            uint32_t rozmiar = 0;
                            if (pobierz_rozmiar_pliku(pelna, &rozmiar) && rozmiar >= 4 && rozmiar <= TAPETY_PLIK_MAX) {
                                if (liczba_tapet < static_cast<int>(TAPETY_MAX - 1)) {
                                    tapety_kopiuj(lista_tapet[liczba_tapet].sciezka, pelna, sizeof(lista_tapet[0].sciezka));
                                    pobierz_nazwe_wyswietlana(pelna, plik, lista_tapet[liczba_tapet].nazwa, sizeof(lista_tapet[0].nazwa));
                                    ++liczba_tapet;
                                }
                            }
                        } else if (is_png && tapety_sciezka_png(pelna)) {
                            uint32_t rozmiar = 0;
                            if (pobierz_rozmiar_pliku(pelna, &rozmiar) && rozmiar >= 8 && rozmiar <= TAPETY_PLIK_MAX) {
                                if (liczba_tapet < static_cast<int>(TAPETY_MAX - 1)) {
                                    tapety_kopiuj(lista_tapet[liczba_tapet].sciezka, pelna, sizeof(lista_tapet[0].sciezka));
                                    pobierz_nazwe_wyswietlana(pelna, plik, lista_tapet[liczba_tapet].nazwa, sizeof(lista_tapet[0].nazwa));
                                    ++liczba_tapet;
                                }
                            }
                        }
                    }
                }
            }
        }

        pos = end;
        if (pos < KATALOG_BUFOR_MAX && bufor[pos] == '\n') ++pos;
    }

    gui_free(bufor);
    sortuj_tapety();
}

void zastosuj(Przycisk*, void*) {
    if (wybrana < 0 || wybrana >= liczba_tapet) return;
    if (tapety_sciezka_powierzchni(lista_tapet[wybrana].sciezka)) {
        if (!zastosuj_obraz(lista_tapet[wybrana].sciezka))
            komunikat->ustaw_tekst("Nie można wczytać obrazu. Pulpit zachował poprzedni obraz.");
        return;
    }
    TapetyZadanie req{}; req.operacja = TAPETY_ZASTOSUJ; req.tryb = tryb;
    tapety_kopiuj(req.sciezka, lista_tapet[wybrana].sciezka, sizeof(req.sciezka));
    TapetyOdpowiedz out{};
    if (!tapety_zapytaj(endpoint, req, &out) || out.status == TAPETY_BLAD)
        komunikat->ustaw_tekst("Nie można wczytać tapety. Pulpit zachował poprzedni obraz.");
    else if (out.status == TAPETY_BLAD_ZAPISU)
        komunikat->ustaw_tekst("Tapeta aktywna. Nie udało się zapisać wyboru na dysku.");
    else komunikat->ustaw_tekst("Tapeta zastosowana i zapisana.");
}

bool pokaz_obraz(const char* path) {
    if (!miniatury || !tapety_sciezka_powierzchni(path)) return false;
    for (int i = 0; i < liczba_tapet; ++i) {
        if (tapety_rowne(lista_tapet[i].sciezka, path)) {
            galeria->ustaw_wybrany_indeks(i);
            wybrana = i;
            return true;
        }
    }
    DekodowanyObraz image{};
    if (!wczytaj_plik_obrazu(path, &image)) return false;
    int slot = liczba_tapet;
    if (slot >= static_cast<int>(TAPETY_MAX)) { gui_free(image.piksele); return false; }
    tapety_kopiuj(lista_tapet[slot].sciezka, path, sizeof(lista_tapet[slot].sciezka));
    const char* name = path;
    for (const char* p = path; *p; ++p) if (*p == '/') name = p + 1;
    tapety_kopiuj(lista_tapet[slot].nazwa, name, sizeof(lista_tapet[slot].nazwa));
    uint32_t* preview = miniatury->obraz(slot);
    const bool scaled = preview && skaluj_obraz(reinterpret_cast<uint32_t*>(image.piksele),
        image.szerokosc, image.wysokosc, image.bajty,
        preview, TAPETY_MINI_W, TAPETY_MINI_H, TAPETY_MINI_W,
        TAPETY_MINI_W * TAPETY_MINI_H * 4, TrybSkalowaniaObrazu::Dopasuj, TAPETY_TLO);
    gui_free(image.piksele);
    if (!scaled) return false;
    ++liczba_tapet;
    galeria->dodaj_element(slot, lista_tapet[slot].nazwa, preview, TAPETY_MINI_W, TAPETY_MINI_H);
    galeria->ustaw_wybrany_indeks(slot);
    wybrana = slot;
    return true;
}

bool pokaz_btp(const char* path) {
    if (!miniatury || !tapety_sciezka(path)) return false;
    for (int i = 0; i < liczba_tapet; ++i) {
        if (tapety_rowne(lista_tapet[i].sciezka, path)) {
            galeria->ustaw_wybrany_indeks(i);
            wybrana = i;
            return true;
        }
    }
    if (liczba_tapet >= static_cast<int>(TAPETY_MAX)) return false;
    const int slot = liczba_tapet;
    if (!miniatury->wczytaj(slot, path)) return false;
    tapety_kopiuj(lista_tapet[slot].sciezka, path, sizeof(lista_tapet[slot].sciezka));
    const char* name = path;
    for (const char* p = path; *p; ++p) if (*p == '/') name = p + 1;
    pobierz_nazwe_wyswietlana(path, name, lista_tapet[slot].nazwa, sizeof(lista_tapet[slot].nazwa));
    galeria->dodaj_element(slot, lista_tapet[slot].nazwa, miniatury->obraz(slot), TAPETY_MINI_W, TAPETY_MINI_H);
    ++liczba_tapet;
    galeria->ustaw_wybrany_indeks(slot);
    wybrana = slot;
    return true;
}

void wybierz_plik(Przycisk*, void* data) {
    auto* okno = static_cast<Okno*>(data);
    if (!okno) return;
    DialogPliku::otworz(okno->zarzadca_popupow(), "/uzytkownicy/Pulpit",
        "Wszystkie obsługiwane obrazy|*.jpg;*.jpeg;*.png;*.btp;;PNG|*.png;;JPEG|*.jpg;*.jpeg;;BTP|*.btp",
        [](DialogPliku*, WynikDialoguPliku result, const char* path, void*) {
            if (result != WynikDialoguPliku::Wybrano || !path || !miniatury) return;
            if (tapety_sciezka(path)) {
                komunikat->ustaw_tekst(pokaz_btp(path) ?
                    "Podgląd BTP gotowy. Wybierz tryb i naciśnij Zastosuj." :
                    "Niepoprawny plik BTP.");
                return;
            }
            if (!pokaz_obraz(path)) {
                komunikat->ustaw_tekst("Niepoprawny lub nieobsługiwany obraz.");
                return;
            }
            komunikat->ustaw_tekst("Podgląd gotowy. Wybierz tryb i naciśnij Zastosuj.");
        }, nullptr, okno->kontrolka_glowna(), "Wybierz tapetę");
}

int znajdz_tapete(const char* path) {
    if (!path) return -1;
    for (int i = 0; i < liczba_tapet; ++i) {
        if (tapety_rowne(lista_tapet[i].sciezka, path)) return i;
    }
    return -1;
}

bool dodaj_placeholder_tapety(const char* path) {
    if (!path || !miniatury || !galeria || liczba_tapet >= static_cast<int>(TAPETY_MAX)) return false;
    const int istnieje = znajdz_tapete(path);
    if (istnieje >= 0) return true;
    if (!tapety_sciezka_powierzchni(path) && !tapety_sciezka(path)) return false;
    uint32_t rozmiar = 0;
    if (!pobierz_rozmiar_pliku(path, &rozmiar) || rozmiar < 4 || rozmiar > TAPETY_PLIK_MAX) return false;
    const int slot = liczba_tapet;
    tapety_kopiuj(lista_tapet[slot].sciezka, path, sizeof(lista_tapet[slot].sciezka));
    const char* name = path;
    for (const char* p = path; *p; ++p) if (*p == '/') name = p + 1;
    pobierz_nazwe_wyswietlana(path, name, lista_tapet[slot].nazwa, sizeof(lista_tapet[slot].nazwa));
    miniatury->ustaw_placeholder(static_cast<unsigned>(slot));
    if (!galeria->dodaj_element(slot, lista_tapet[slot].nazwa, miniatury->obraz(static_cast<unsigned>(slot)),
                                TAPETY_MINI_W, TAPETY_MINI_H)) {
        lista_tapet[slot] = {};
        return false;
    }
    ++liczba_tapet;
    return true;
}

bool sciezka_miniatury_systemowej(const char* path, char* out, size_t cap) {
    if (!path || !out || cap == 0) return false;
    constexpr char prefix[] = "/tapety/";
    size_t p = 0;
    for (; prefix[p]; ++p) if (path[p] != prefix[p]) return false;
    const char* name = path + p;
    if (!name[0]) return false;
    size_t dot = 0;
    while (name[dot] && name[dot] != '/') {
        if (name[dot] == '.') break;
        ++dot;
    }
    if (!dot || name[dot] != '.') return false;
    for (size_t i = dot; name[i]; ++i) if (name[i] == '/') return false;
    size_t o = 0;
    constexpr char mini_prefix[] = "/tapety/miniatury/";
    for (; mini_prefix[o] && o + 1 < cap; ++o) out[o] = mini_prefix[o];
    for (size_t i = 0; i < dot && o + 1 < cap; ++i) out[o++] = name[i];
    constexpr char ext[] = ".btp";
    for (size_t i = 0; ext[i] && o + 1 < cap; ++i) out[o++] = ext[i];
    out[o] = '\0';
    return true;
}

bool wczytaj_miniature(int i) {
    if (!miniatury || i < 0 || i >= liczba_tapet) return false;
    bool ok = false;
    char mini_path[TAPETY_SCIEZKA_MAX]{};
    if (sciezka_miniatury_systemowej(lista_tapet[i].sciezka, mini_path, sizeof(mini_path))) {
        ok = miniatury->wczytaj(static_cast<unsigned>(i), mini_path, true);
        if (ok) {
            ++miniatury_btp_wczytane;
        }
    }
    if (tapety_sciezka_powierzchni(lista_tapet[i].sciezka)) {
        DekodowanyObraz img{};
        if (!ok) {
            ++miniatury_fallback_pelny;
        }
        if (!ok && wczytaj_plik_obrazu(lista_tapet[i].sciezka, &img)) {
            uint32_t* preview = miniatury->obraz(static_cast<unsigned>(i));
            ok = preview && skaluj_obraz(reinterpret_cast<uint32_t*>(img.piksele),
                img.szerokosc, img.wysokosc, img.bajty,
                preview, TAPETY_MINI_W, TAPETY_MINI_H, TAPETY_MINI_W,
                TAPETY_MINI_W * TAPETY_MINI_H * 4, TrybSkalowaniaObrazu::Dopasuj, TAPETY_TLO);
            gui_free(img.piksele);
        }
    } else {
        if (!ok) {
            ++miniatury_fallback_pelny;
            ok = miniatury->wczytaj(static_cast<unsigned>(i), lista_tapet[i].sciezka);
        }
    }
    if (!ok) {
        miniatury->ustaw_placeholder(static_cast<unsigned>(i));
    }
    if (galeria) galeria->odswiez_element(i);
    return ok;
}

class PanelTapet : public Panel {
public:
    bool czy_wymaga_animacji() const override {
        return nastepna_miniatura_ < liczba_tapet || Panel::czy_wymaga_animacji();
    }

    void krok_animacji(uint64_t teraz_ns = 0) override {
        Panel::krok_animacji(teraz_ns);
        if (nastepna_miniatura_ >= liczba_tapet) return;
        const int i = nastepna_miniatura_++;
        const uint64_t start = gui_czas_monotoniczny_ns();
        if (wczytaj_miniature(i)) ++poprawne_;
        ++proby_;
        log_perf("miniatura krok", (gui_czas_monotoniczny_ns() - start) / 1000U);
#if BURSZTYNDIAGTAPETYPERF
        if (proby_ == 1) {
            log_perf("pierwsza miniatura od startu", (gui_czas_monotoniczny_ns() - start_ns) / 1000U);
        }
        if (nastepna_miniatura_ >= liczba_tapet) {
            log("[TAPETY PERF] miniatury gotowe ");
            log_perf_uint64(poprawne_);
            log("/");
            log_perf_uint64(proby_);
            log(" w ");
            log_perf_uint64((gui_czas_monotoniczny_ns() - start_ns) / 1000U);
            log(" us\n");
            log("[TAPETY PERF] miniatury BTP ");
            log_perf_uint64(miniatury_btp_wczytane);
            log(" fallback pelny ");
            log_perf_uint64(miniatury_fallback_pelny);
            // miniatury_pamiec_bajty to calkowita pojemnosc alokacji dla wszystkich TAPETY_MAX (65) elementow, 
            // a nie tylko rozmiar uzytych miniatur (czyli 65 * 160 * 90 * 4 = 3744000 bajtow).
            log(" cache capacity bajty ");
            log_perf_uint64(miniatury_pamiec_bajty);
            log("\n");
        }
#endif
    }

private:
    int nastepna_miniatura_{0};
    unsigned poprawne_{0};
    unsigned proby_{0};
};
} // namespace

extern "C" __attribute__((noreturn)) void _start() {
    start_ns = gui_czas_monotoniczny_ns();
#if BURSZTYNDIAGTAPETYPERF
    log("[TAPETY PERF] Start.\n");
#endif
    char argument[24]{};
    if (pobierz_argument_startowy(argument, sizeof(argument)) &&
        tapety_rowne(argument, "--restore", sizeof(argument))) {
        endpoint = ipc_create();
        TapetyZadanie req{}; req.operacja = TAPETY_STAN;
        TapetyOdpowiedz state{};
        if (endpoint && tapety_zapytaj(endpoint, req, &state) &&
            state.status == TAPETY_OK && tapety_sciezka_powierzchni(state.sciezka)) {
            tryb = state.tryb <= 3 ? state.tryb : 1;
            (void)zastosuj_obraz(state.sciezka);
        }
        if (endpoint) ipc_close(endpoint);
        bws_zakoncz_proces();
    }
    AplikacjaGUI app;
    Prostokat work = Okno::pobierz_obszar_roboczy();
    const uint32_t procent = ui_skala_procent;
    int cw = ui_skaluj(174, procent);
    int ch = ui_skaluj(124, procent);
    int sw = ui_skaluj(15, procent);
    int pad_h = ui_skaluj(24, procent);

    int max_cols = (work.szerokosc - pad_h - sw - 2 - ui_skaluj(40, procent)) / cw;
    int target_cols = max_cols >= 5 ? 5 : przytnij(max_cols, 1, 5);
    int win_w = target_cols * 174 + 15 + 2 + 24;

    PanelTapet root;
    Okno okno("Tapety", win_w, 600);
    okno.ustaw_kontrolke_glowna(&root);
    app.ustaw_glowne_okno(&okno);

    endpoint = ipc_create();
    if (!endpoint) {
        log("[TAPETY APP] BLAD inicjalizacji okna lub IPC.\n");
        gui_zakoncz_aplikacje();
    }
    root.ustaw_orientacje(Orientacja::Pionowa);
    root.ustaw_wypelnienie(Marginesy(12)); root.ustaw_odstep(8);
    root.dodaj<Etykieta>("Tapeta pulpitu");
    galeria = &root.dodaj<WidokMiniatur>();
    galeria->ustaw_pelne_wiersze(true);
    galeria->ustaw_rozmiar_elementu(174, 124);
    galeria->ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
    galeria->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);

    odkryj_tapety();

    MiniaturyTapet mini;
    const uint32_t poj = TAPETY_MAX;
    if (!mini.przygotuj(poj)) {
        log("[TAPETY APP] BLAD alokacji bufora miniatur.\n");
        gui_zakoncz_aplikacje();
    }
    miniatury = &mini;
    miniatury_pamiec_bajty = static_cast<uint64_t>(poj) * TAPETY_MINI_W * TAPETY_MINI_H * sizeof(uint32_t);

    for (int i = 0; i < liczba_tapet; ++i) {
        mini.ustaw_placeholder(static_cast<unsigned>(i));
        galeria->dodaj_element(i, lista_tapet[i].nazwa, mini.obraz(static_cast<unsigned>(i)), TAPETY_MINI_W, TAPETY_MINI_H);
    }

    TapetyZadanie req{}; req.operacja = TAPETY_STAN;
    TapetyOdpowiedz stan{};
    wybrana = 0;
    if (tapety_zapytaj(endpoint, req, &stan) && stan.status == TAPETY_OK) {
        tryb = stan.tryb <= 3 ? stan.tryb : 1;
        if (stan.sciezka[0] && znajdz_tapete(stan.sciezka) < 0) {
            (void)dodaj_placeholder_tapety(stan.sciezka);
        }
        for (int i = 0; i < liczba_tapet; ++i) {
            if (tapety_rowne(stan.sciezka, lista_tapet[i].sciezka)) {
                wybrana = i;
                break;
            }
        }
    }
    galeria->przy_zmianie_zaznaczenia([](WidokMiniatur*, int i, uint64_t, void*) {
        if (i >= 0 && i < liczba_tapet) {
            wybrana = i; komunikat->ustaw_tekst("Wybór oczekuje na Zastosuj.");
        }
    });
    auto& lista = root.dodaj<ListaRozwijana>();
    lista.dodaj_element("Dopasuj"); lista.dodaj_element("Wypełnij"); lista.dodaj_element("Wyśrodkuj"); lista.dodaj_element("Rozciągnij");
    lista.ustaw_wybrany_indeks(tryb);
    lista.przy_zmianie_wyboru([](ListaRozwijana*, int i, void*) { if (i >= 0 && i <= 3) tryb = i; });
    auto& row = root.dodaj<Panel>(); row.ustaw_orientacje(Orientacja::Pozioma); row.ustaw_odstep(12);
    row.dodaj<Przycisk>("Wybierz plik...").przy_kliknieciu(wybierz_plik, &okno);
    row.dodaj<Przycisk>("Zastosuj").przy_kliknieciu(zastosuj);
    row.dodaj<Przycisk>("Anuluj").przy_kliknieciu([](Przycisk*, void* p) { static_cast<Okno*>(p)->zamknij(); }, &okno);
    komunikat = &root.dodaj<Etykieta>(liczba_tapet > 0 ? "Wczytywanie miniatur..." : "Brak dostępnych tapet w /tapety.");

    okno.przelicz_uklad();
    int stale_h = okno.wysokosc() - galeria->obszar().wysokosc;
    int avail_for_gallery = work.wysokosc - stale_h;
    int target_rows = 3;
    if (avail_for_gallery >= 4 * ch + 2) {
        target_rows = 4;
    } else if (avail_for_gallery >= 3 * ch + 2) {
        target_rows = 3;
    } else if (avail_for_gallery >= 2 * ch + 2) {
        target_rows = 2;
    } else {
        target_rows = 1;
    }
    int win_h = target_rows * ch + 2 + stale_h;
    okno.ustaw_rozmiar(ui_skaluj(win_w, procent), win_h);
    okno.wysrodkuj();
    okno.przelicz_uklad();
    if (liczba_tapet > 0) {
        galeria->ustaw_wybrany_indeks(wybrana);
    }
    log_perf("gotowe do pierwszego rysowania", (gui_czas_monotoniczny_ns() - start_ns) / 1000U);
    (void)app.uruchom(okno);
    ipc_close(endpoint);
    bws_zakoncz_proces();
}
