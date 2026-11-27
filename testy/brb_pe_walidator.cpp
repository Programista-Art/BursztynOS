/*
 * BRB - niezalezny walidator obrazu PE32+ aplikacji UEFI (test T-PE, docs/brb/plan-testow.md).
 *
 * Parser napisany od zera wg "PE Format" (Microsoft) i UEFI 2.10 (2.1.1 UEFI Images). Nie korzysta z BFD
 * ani z kodu BRB - jest niezalezny od narzedzi, ktore zbudowaly obraz.
 *
 * Uzycie:
 *   brb_pe_walidator <obraz.efi> [--wejscie-rva 0xRVA] [--relokacje-elf N]   walidacja jednego obrazu
 *   brb_pe_walidator --mutacje <obraz.efi>                                    test negatywny: kazda mutacja
 *                                                                             prawdziwego obrazu musi zostac odrzucona
 * Kod wyjscia 0 = obraz poprawny (albo wszystkie mutacje odrzucone).
 */
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

constexpr uint16_t MASZYNA_AMD64 = 0x8664;
constexpr uint16_t MAGIC_PE32PLUS = 0x20B;
constexpr uint16_t PODSYSTEM_EFI_APP = 10;
constexpr uint16_t CH_RELOCS_STRIPPED = 0x0001, CH_EXECUTABLE = 0x0002, CH_DLL = 0x2000;
constexpr uint32_t SCN_CODE = 0x20, SCN_IDATA = 0x40, SCN_UDATA = 0x80;
constexpr uint32_t SCN_EXEC = 0x20000000, SCN_READ = 0x40000000, SCN_WRITE = 0x80000000;
constexpr int KAT_EKSPORT = 0, KAT_IMPORT = 1, KAT_ZASOBY = 2, KAT_CERT = 4, KAT_RELOC = 5, KAT_TLS = 9,
              KAT_IAT = 12, KAT_DELAY = 13, KAT_CLR = 14;
constexpr int REL_ABSOLUTE = 0, REL_DIR64 = 10;

struct Sekcja {
    std::string nazwa;
    uint32_t vsize, va, rsize, rptr, ch;
};

struct Obraz {
    std::vector<uint8_t> plik;
    uint32_t pe = 0, opt = 0;
    uint16_t liczba_sekcji = 0, rozmiar_opt = 0, cechy = 0;
    uint64_t baza = 0;
    uint32_t wejscie = 0, wyr_sekcji = 0, wyr_pliku = 0, rozmiar_obrazu = 0, rozmiar_naglowkow = 0;
    uint16_t podsystem = 0;
    uint32_t liczba_katalogow = 0;
    uint32_t kat_rva[16]{}, kat_rozmiar[16]{};
    std::vector<Sekcja> sekcje;
    uint32_t relokacje_dir64 = 0;
};

struct Wynik {
    std::vector<std::string> bledy;
    std::vector<std::string> info;
    void blad(const std::string& b) { bledy.push_back(b); }
};

bool zakres(uint64_t poczatek, uint64_t dlugosc, uint64_t calosc) {
    return poczatek <= calosc && dlugosc <= calosc - poczatek;
}
uint16_t u16(const std::vector<uint8_t>& d, uint64_t o) { return uint16_t(d[o] | d[o + 1] << 8); }
uint32_t u32(const std::vector<uint8_t>& d, uint64_t o) { return uint32_t(u16(d, o) | uint32_t(u16(d, o + 2)) << 16); }
uint64_t u64(const std::vector<uint8_t>& d, uint64_t o) { return u32(d, o) | uint64_t(u32(d, o + 4)) << 32; }
bool potega2(uint64_t x) { return x && !(x & (x - 1)); }
uint64_t w_gore(uint64_t x, uint64_t a) { return (x + a - 1) / a * a; }
std::string hex(uint64_t v) { char b[32]; std::snprintf(b, sizeof b, "0x%llX", (unsigned long long)v); return b; }

const Sekcja* sekcja_rva(const Obraz& o, uint64_t rva, uint64_t dlugosc) {
    for (const Sekcja& s : o.sekcje) {
        const uint64_t rozmiar = s.vsize ? s.vsize : s.rsize;
        if (rva >= s.va && zakres(rva - s.va, dlugosc, rozmiar)) return &s;
    }
    return nullptr;
}

// Offset w pliku dla RVA lezacego w zainicjalizowanej czesci sekcji (albo w naglowkach).
bool rva_na_plik(const Obraz& o, uint64_t rva, uint64_t dlugosc, uint64_t* off) {
    const Sekcja* s = sekcja_rva(o, rva, dlugosc);
    if (!s || !zakres(rva - s->va, dlugosc, s->rsize)) return false;
    *off = uint64_t(s->rptr) + (rva - s->va);
    return zakres(*off, dlugosc, o.plik.size());
}

Wynik waliduj(const std::vector<uint8_t>& plik, Obraz* wy, int64_t oczekiwane_wejscie, int64_t relokacje_elf) {
    Wynik w;
    Obraz o;
    o.plik = plik;
    const auto& d = o.plik;
    // ---- DOS ----
    if (d.size() < 64 || d[0] != 'M' || d[1] != 'Z') { w.blad("brak naglowka DOS 'MZ'"); return w; }
    o.pe = u32(d, 0x3C);
    if (o.pe < 64 || (o.pe & 7) || !zakres(o.pe, 24, d.size())) { w.blad("e_lfanew poza plikiem albo niewyrownany"); return w; }
    if (std::memcmp(&d[o.pe], "PE\0\0", 4) != 0) { w.blad("brak sygnatury 'PE\\0\\0'"); return w; }
    // ---- COFF ----
    const uint32_t coff = o.pe + 4;
    const uint16_t maszyna = u16(d, coff);
    o.liczba_sekcji = u16(d, coff + 2);
    const uint32_t tablica_symboli = u32(d, coff + 8), liczba_symboli = u32(d, coff + 12);
    o.rozmiar_opt = u16(d, coff + 16);
    o.cechy = u16(d, coff + 18);
    if (maszyna != MASZYNA_AMD64) w.blad("Machine " + hex(maszyna) + " != 0x8664 (x86-64)");
    if (o.liczba_sekcji == 0 || o.liczba_sekcji > 16) w.blad("liczba sekcji poza zakresem 1..16");
    if (!(o.cechy & CH_EXECUTABLE)) w.blad("brak IMAGE_FILE_EXECUTABLE_IMAGE");
    if (o.cechy & CH_RELOCS_STRIPPED) w.blad("IMAGE_FILE_RELOCS_STRIPPED: obraz nierelokowalny");
    if (o.cechy & CH_DLL) w.blad("obraz oznaczony jako DLL");
    if (tablica_symboli || liczba_symboli) w.blad("obraz zawiera tablice symboli COFF (nie zostal okrojony)");
    // ---- Opcjonalny naglowek PE32+ ----
    o.opt = coff + 20;
    if (o.rozmiar_opt < 112 || !zakres(o.opt, o.rozmiar_opt, d.size())) { w.blad("za krotki naglowek opcjonalny"); return w; }
    if (u16(d, o.opt) != MAGIC_PE32PLUS) { w.blad("Magic naglowka opcjonalnego != 0x20B (PE32+)"); return w; }
    o.wejscie = u32(d, o.opt + 16);
    o.baza = u64(d, o.opt + 24);
    o.wyr_sekcji = u32(d, o.opt + 32);
    o.wyr_pliku = u32(d, o.opt + 36);
    o.rozmiar_obrazu = u32(d, o.opt + 56);
    o.rozmiar_naglowkow = u32(d, o.opt + 60);
    o.podsystem = u16(d, o.opt + 68);
    o.liczba_katalogow = u32(d, o.opt + 108);
    if (o.podsystem != PODSYSTEM_EFI_APP) w.blad("Subsystem " + std::to_string(o.podsystem) + " != 10 (EFI application)");
    if (!potega2(o.wyr_pliku) || o.wyr_pliku < 512 || o.wyr_pliku > 65536) w.blad("FileAlignment niepoprawne");
    if (!potega2(o.wyr_sekcji) || o.wyr_sekcji < 4096 || o.wyr_sekcji < o.wyr_pliku) w.blad("SectionAlignment niepoprawne (wymagane >= 4096 i >= FileAlignment)");
    if (o.baza & 0xFFFF) w.blad("ImageBase niewyrownane do 64 KiB");
    if (o.liczba_katalogow > 16 || o.liczba_katalogow <= KAT_RELOC || 112u + o.liczba_katalogow * 8u > o.rozmiar_opt)
        w.blad("NumberOfRvaAndSizes niespojne z rozmiarem naglowka");
    if (!w.bledy.empty()) return w;
    for (uint32_t i = 0; i < o.liczba_katalogow; ++i) {
        o.kat_rva[i] = u32(d, o.opt + 112 + i * 8);
        o.kat_rozmiar[i] = u32(d, o.opt + 116 + i * 8);
    }
    // ---- Tablica sekcji ----
    const uint64_t tab = uint64_t(o.opt) + o.rozmiar_opt;
    if (!zakres(tab, uint64_t(o.liczba_sekcji) * 40, d.size())) { w.blad("tablica sekcji poza plikiem"); return w; }
    if (o.rozmiar_naglowkow < tab + o.liczba_sekcji * 40u || o.rozmiar_naglowkow % o.wyr_pliku)
        w.blad("SizeOfHeaders za male albo niewyrownane");
    for (uint16_t i = 0; i < o.liczba_sekcji; ++i) {
        const uint64_t e = tab + i * 40u;
        Sekcja s;
        char nazwa[9]{};
        std::memcpy(nazwa, &d[e], 8);
        s.nazwa = nazwa;
        s.vsize = u32(d, e + 8); s.va = u32(d, e + 12); s.rsize = u32(d, e + 16); s.rptr = u32(d, e + 20);
        s.ch = u32(d, e + 36);
        o.sekcje.push_back(s);
    }
    uint64_t poprzedni_koniec_va = o.rozmiar_naglowkow, poprzedni_koniec_pliku = o.rozmiar_naglowkow;
    uint64_t koniec_danych_pliku = o.rozmiar_naglowkow;
    for (const Sekcja& s : o.sekcje) {
        const std::string n = "sekcja " + s.nazwa + ": ";
        static const char* const dozwolone[] = {".text", ".rdata", ".data", ".bss", ".reloc"};
        bool znana = false;
        for (const char* z : dozwolone) znana = znana || s.nazwa == z;
        if (!znana) w.blad(n + "nazwa spoza zestawu .text/.rdata/.data/.bss/.reloc (sekcja osierocona?)");
        if (s.va % o.wyr_sekcji) w.blad(n + "VirtualAddress niewyrownany do SectionAlignment");
        if (s.va < poprzedni_koniec_va) w.blad(n + "nachodzi na poprzednia sekcje albo naglowki (VA)");
        const uint64_t rozmiar_v = std::max<uint64_t>(s.vsize, s.rsize);
        if (rozmiar_v == 0) w.blad(n + "zerowy rozmiar");
        poprzedni_koniec_va = uint64_t(s.va) + w_gore(rozmiar_v ? rozmiar_v : 1, o.wyr_sekcji);
        if (s.rsize) {
            if (s.rptr % o.wyr_pliku || s.rsize % o.wyr_pliku) w.blad(n + "dane surowe niewyrownane do FileAlignment");
            if (!zakres(s.rptr, s.rsize, d.size())) w.blad(n + "dane surowe poza plikiem");
            if (s.rptr < poprzedni_koniec_pliku) w.blad(n + "dane surowe nachodza na poprzednie");
            poprzedni_koniec_pliku = uint64_t(s.rptr) + s.rsize;
            koniec_danych_pliku = std::max(koniec_danych_pliku, poprzedni_koniec_pliku);
            if (s.vsize && w_gore(s.vsize, o.wyr_pliku) < s.rsize) w.blad(n + "SizeOfRawData wieksze niz potrzebne dla VirtualSize");
        }
        if ((s.ch & SCN_WRITE) && (s.ch & SCN_EXEC)) w.blad(n + "jednoczesnie zapisywalna i wykonywalna (W+X)");
        if ((s.ch & SCN_EXEC) && !(s.ch & SCN_CODE)) w.blad(n + "wykonywalna bez IMAGE_SCN_CNT_CODE");
        if (!(s.ch & SCN_READ)) w.blad(n + "brak prawa odczytu");
        if (!(s.ch & (SCN_CODE | SCN_IDATA | SCN_UDATA))) w.blad(n + "brak typu zawartosci");
    }
    if (o.rozmiar_obrazu != poprzedni_koniec_va || o.rozmiar_obrazu % o.wyr_sekcji)
        w.blad("SizeOfImage " + hex(o.rozmiar_obrazu) + " != wyrownany koniec ostatniej sekcji " + hex(poprzedni_koniec_va));
    if (koniec_danych_pliku != d.size())
        w.blad("dodatkowe bajty za ostatnia sekcja (" + std::to_string(d.size() - koniec_danych_pliku) + " B)");
    // ---- Punkt wejscia ----
    const Sekcja* sw = sekcja_rva(o, o.wejscie, 1);
    if (!o.wejscie || !sw || !(sw->ch & SCN_EXEC)) w.blad("AddressOfEntryPoint " + hex(o.wejscie) + " poza sekcja wykonywalna");
    if (oczekiwane_wejscie >= 0 && o.wejscie != uint64_t(oczekiwane_wejscie))
        w.blad("AddressOfEntryPoint " + hex(o.wejscie) + " != RVA efi_main " + hex(uint64_t(oczekiwane_wejscie)));
    // ---- Katalogi: brak importow/eksportow/TLS ----
    const int zabronione[] = {KAT_EKSPORT, KAT_IMPORT, KAT_ZASOBY, KAT_CERT, KAT_TLS, KAT_IAT, KAT_DELAY, KAT_CLR};
    for (int k : zabronione)
        if (uint32_t(k) < o.liczba_katalogow && (o.kat_rva[k] || o.kat_rozmiar[k]))
            w.blad("niedozwolony katalog danych nr " + std::to_string(k) + " (importy/eksporty/TLS/zasoby/certyfikat)");
    // ---- Relokacje bazowe ----
    const uint32_t r_rva = o.kat_rva[KAT_RELOC], r_rozmiar = o.kat_rozmiar[KAT_RELOC];
    uint64_t r_off = 0;
    if (!r_rva || r_rozmiar < 8) {
        w.blad("brak katalogu relokacji bazowych");
    } else if (!rva_na_plik(o, r_rva, r_rozmiar, &r_off)) {
        w.blad("katalog relokacji poza danymi sekcji");
    } else {
        const Sekcja* rs = sekcja_rva(o, r_rva, r_rozmiar);
        if (rs->nazwa != ".reloc") w.blad("katalog relokacji poza sekcja .reloc");
        if (rs->ch & (SCN_WRITE | SCN_EXEC)) w.blad("sekcja .reloc zapisywalna albo wykonywalna");
        uint64_t p = 0;
        std::vector<uint32_t> miejsca;
        while (p < r_rozmiar) {
            if (r_rozmiar - p < 8) { w.blad("urwany blok relokacji"); break; }
            const uint32_t strona = u32(d, r_off + p), rozmiar_bloku = u32(d, r_off + p + 4);
            if (strona % 4096) w.blad("PageRVA bloku relokacji niewyrownany");
            if (rozmiar_bloku < 8 || rozmiar_bloku % 4 || rozmiar_bloku > r_rozmiar - p) { w.blad("niepoprawny SizeOfBlock"); break; }
            for (uint64_t e = 8; e < rozmiar_bloku; e += 2) {
                const uint16_t wpis = u16(d, r_off + p + e);
                const int typ = wpis >> 12;
                const uint32_t rva = strona + (wpis & 0xFFF);
                if (typ == REL_ABSOLUTE) continue;  // wypelnienie bloku do 32 bitow
                if (typ != REL_DIR64) { w.blad("nieobslugiwany typ relokacji " + std::to_string(typ)); continue; }
                uint64_t off = 0;
                if (!rva_na_plik(o, rva, 8, &off)) { w.blad("relokacja DIR64 " + hex(rva) + " poza zainicjalizowanymi danymi"); continue; }
                const Sekcja* cel = sekcja_rva(o, rva, 8);
                if (cel->ch & SCN_EXEC) w.blad("relokacja DIR64 w sekcji wykonywalnej " + hex(rva));
                const uint64_t wartosc = u64(d, off);
                if (wartosc < o.baza || wartosc - o.baza >= o.rozmiar_obrazu)
                    w.blad("relokowany wskaznik " + hex(rva) + "=" + hex(wartosc) + " nie wskazuje wnetrza obrazu");
                for (uint32_t m : miejsca) if (m == rva) w.blad("zduplikowana relokacja " + hex(rva));
                miejsca.push_back(rva);
                ++o.relokacje_dir64;
            }
            p += rozmiar_bloku;
        }
        if (o.relokacje_dir64 == 0) w.blad("katalog relokacji nie zawiera zadnego wpisu DIR64 (relokacja pozorna)");
        if (relokacje_elf >= 0 && o.relokacje_dir64 != uint64_t(relokacje_elf))
            w.blad("liczba DIR64 (" + std::to_string(o.relokacje_dir64) + ") != liczba R_X86_64_64 w obiektach (" +
                   std::to_string(relokacje_elf) + ")");
        // Symulacja przeniesienia: obraz w pamieci pod inna baze; tylko miejsca relokacji moga sie zmienic
        // i musza wskazywac to samo RVA w nowej bazie.
        if (w.bledy.empty()) {
            std::vector<uint8_t> pamiec(o.rozmiar_obrazu, 0);
            std::memcpy(pamiec.data(), d.data(), o.rozmiar_naglowkow);
            for (const Sekcja& s : o.sekcje)
                if (s.rsize) std::memcpy(&pamiec[s.va], &d[s.rptr], std::min<uint64_t>(s.rsize, s.vsize ? s.vsize : s.rsize));
            std::vector<uint8_t> przed = pamiec;
            const uint64_t nowa_baza = o.baza + 0x7FFF0000ull;
            for (uint32_t m : miejsca) {
                uint64_t v;
                std::memcpy(&v, &pamiec[m], 8);
                v += nowa_baza - o.baza;
                std::memcpy(&pamiec[m], &v, 8);
            }
            size_t zmienione = 0;
            for (size_t i = 0; i < pamiec.size(); ++i) {
                if (pamiec[i] == przed[i]) continue;
                bool w_miejscu = false;
                for (uint32_t m : miejsca) w_miejscu = w_miejscu || (i >= m && i < m + 8);
                if (!w_miejscu) w.blad("symulacja relokacji zmienila bajt poza miejscem relokacji");
                ++zmienione;
            }
            for (uint32_t m : miejsca) {
                uint64_t v;
                std::memcpy(&v, &pamiec[m], 8);
                if (v < nowa_baza || v - nowa_baza >= o.rozmiar_obrazu) w.blad("po przeniesieniu wskaznik poza obrazem");
            }
            w.info.push_back("symulacja przeniesienia o 0x7FFF0000: zmienione bajty=" + std::to_string(zmienione) +
                             " (tylko w " + std::to_string(miejsca.size()) + " miejscach relokacji)");
        }
    }
    if (wy) *wy = o;
    return w;
}

std::vector<uint8_t> wczytaj(const char* sciezka) {
    std::vector<uint8_t> d;
    FILE* f = std::fopen(sciezka, "rb");
    if (!f) { std::fprintf(stderr, "Nie mozna otworzyc %s: %s\n", sciezka, std::strerror(errno)); std::exit(2); }
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    if (d.size() > 64u * 1024 * 1024) { std::fprintf(stderr, "Plik za duzy\n"); std::exit(2); }
    return d;
}

int raport(const char* sciezka, int64_t wejscie, int64_t relokacje) {
    Obraz o;
    const Wynik w = waliduj(wczytaj(sciezka), &o, wejscie, relokacje);
    std::printf("[T-PE] obraz: %s (%zu B)\n", sciezka, o.plik.size());
    if (!o.plik.empty() && w.bledy.empty()) {
        std::printf("[T-PE] PE32+ Machine=0x8664 Subsystem=%u ImageBase=%s SectionAlignment=%s FileAlignment=%s\n",
                    o.podsystem, hex(o.baza).c_str(), hex(o.wyr_sekcji).c_str(), hex(o.wyr_pliku).c_str());
        std::printf("[T-PE] SizeOfImage=%s SizeOfHeaders=%s AddressOfEntryPoint=%s\n", hex(o.rozmiar_obrazu).c_str(),
                    hex(o.rozmiar_naglowkow).c_str(), hex(o.wejscie).c_str());
        for (const Sekcja& s : o.sekcje)
            std::printf("[T-PE]   %-7s VA=%s VirtualSize=%s RawPtr=%s RawSize=%s %s%s%s\n", s.nazwa.c_str(), hex(s.va).c_str(),
                        hex(s.vsize).c_str(), hex(s.rptr).c_str(), hex(s.rsize).c_str(), (s.ch & SCN_READ) ? "R" : "-",
                        (s.ch & SCN_WRITE) ? "W" : "-", (s.ch & SCN_EXEC) ? "X" : "-");
        std::printf("[T-PE] relokacje bazowe: katalog RVA=%s rozmiar=%u, wpisy DIR64=%u\n", hex(o.kat_rva[KAT_RELOC]).c_str(),
                    o.kat_rozmiar[KAT_RELOC], o.relokacje_dir64);
        std::printf("[T-PE] importy: brak; eksporty: brak; TLS: brak\n");
    }
    for (const auto& i : w.info) std::printf("[T-PE] %s\n", i.c_str());
    for (const auto& b : w.bledy) std::printf("[T-PE] BLAD: %s\n", b.c_str());
    std::printf("[T-PE] %s\n", w.bledy.empty() ? "WYNIK: obraz poprawny" : "WYNIK: obraz ODRZUCONY");
    return w.bledy.empty() ? 0 : 1;
}

int mutacje(const char* sciezka) {
    const std::vector<uint8_t> wzor = wczytaj(sciezka);
    Obraz o;
    if (!waliduj(wzor, &o, -1, -1).bledy.empty()) { std::printf("[T-PE-MUT] wzorzec niepoprawny\n"); return 1; }
    const uint32_t coff = o.pe + 4, opt = o.opt, tab = o.opt + o.rozmiar_opt;
    auto p16 = [](std::vector<uint8_t>& d, uint64_t off, uint16_t v) { d[off] = uint8_t(v); d[off + 1] = uint8_t(v >> 8); };
    auto p32 = [&](std::vector<uint8_t>& d, uint64_t off, uint32_t v) { p16(d, off, uint16_t(v)); p16(d, off + 2, uint16_t(v >> 16)); };
    int indeks_text = -1, indeks_rdata = -1, indeks_reloc = -1;
    for (size_t i = 0; i < o.sekcje.size(); ++i) {
        if (o.sekcje[i].nazwa == ".text") indeks_text = int(i);
        if (o.sekcje[i].nazwa == ".rdata") indeks_rdata = int(i);
        if (o.sekcje[i].nazwa == ".reloc") indeks_reloc = int(i);
    }
    if (indeks_text < 0 || indeks_rdata < 0 || indeks_reloc < 0) { std::printf("[T-PE-MUT] brak oczekiwanych sekcji\n"); return 1; }
    uint64_t r_off = 0;
    rva_na_plik(o, o.kat_rva[KAT_RELOC], o.kat_rozmiar[KAT_RELOC], &r_off);
    struct Mutacja { const char* nazwa; std::function<void(std::vector<uint8_t>&)> f; };
    const std::vector<Mutacja> lista = {
        {"zly magic MZ", [&](auto& d) { d[0] = 'X'; }},
        {"e_lfanew poza plikiem", [&](auto& d) { p32(d, 0x3C, uint32_t(d.size())); }},
        {"zla sygnatura PE", [&](auto& d) { d[o.pe + 1] = 'X'; }},
        {"Machine i386 (0x14C)", [&](auto& d) { p16(d, coff, 0x14C); }},
        {"Machine AArch64 (0xAA64)", [&](auto& d) { p16(d, coff, 0xAA64); }},
        {"Magic PE32 (0x10B)", [&](auto& d) { p16(d, opt, 0x10B); }},
        {"Subsystem 3 (konsola Windows)", [&](auto& d) { p16(d, opt + 68, 3); }},
        {"Subsystem 11 (sterownik uslug rozruchowych)", [&](auto& d) { p16(d, opt + 68, 11); }},
        {"RELOCS_STRIPPED", [&](auto& d) { p16(d, coff + 18, uint16_t(o.cechy | CH_RELOCS_STRIPPED)); }},
        {"flaga DLL", [&](auto& d) { p16(d, coff + 18, uint16_t(o.cechy | CH_DLL)); }},
        {"SectionAlignment 0x800", [&](auto& d) { p32(d, opt + 32, 0x800); }},
        {"FileAlignment 0x300", [&](auto& d) { p32(d, opt + 36, 0x300); }},
        {"SizeOfImage za male", [&](auto& d) { p32(d, opt + 56, o.rozmiar_obrazu - 0x1000); }},
        {"wejscie poza obrazem", [&](auto& d) { p32(d, opt + 16, o.rozmiar_obrazu + 0x10); }},
        {"wejscie w .rdata", [&](auto& d) { p32(d, opt + 16, o.sekcje[indeks_rdata].va); }},
        {".text zapisywalna (W+X)", [&](auto& d) { p32(d, tab + indeks_text * 40u + 36, o.sekcje[indeks_text].ch | SCN_WRITE); }},
        {".rdata wykonywalna", [&](auto& d) { p32(d, tab + indeks_rdata * 40u + 36, o.sekcje[indeks_rdata].ch | SCN_EXEC); }},
        {"nakladanie VA sekcji", [&](auto& d) { p32(d, tab + indeks_rdata * 40u + 12, o.sekcje[indeks_text].va); }},
        {"dane sekcji poza plikiem", [&](auto& d) { p32(d, tab + indeks_rdata * 40u + 20, uint32_t(d.size())); }},
        {"obcieta sekcja .reloc", [&](auto& d) { d.resize(o.sekcje[indeks_reloc].rptr + 4); }},
        {"dodatkowe bajty na koncu", [&](auto& d) { d.push_back(0); }},
        {"katalog importow niezerowy", [&](auto& d) { p32(d, opt + 112 + KAT_IMPORT * 8, o.sekcje[indeks_rdata].va); p32(d, opt + 116 + KAT_IMPORT * 8, 20); }},
        {"katalog TLS niezerowy", [&](auto& d) { p32(d, opt + 112 + KAT_TLS * 8, o.sekcje[indeks_rdata].va); p32(d, opt + 116 + KAT_TLS * 8, 40); }},
        {"brak katalogu relokacji", [&](auto& d) { p32(d, opt + 112 + KAT_RELOC * 8, 0); p32(d, opt + 116 + KAT_RELOC * 8, 0); }},
        {"relokacje pozorne (same ABSOLUTE)", [&](auto& d) {
             for (uint64_t e = 8; e < o.kat_rozmiar[KAT_RELOC]; e += 2) p16(d, r_off + e, 0);
         }},
        {"typ relokacji HIGHLOW (3)", [&](auto& d) { p16(d, r_off + 8, uint16_t((3 << 12) | (u16(d, r_off + 8) & 0xFFF))); }},
        {"relokacja w .text", [&](auto& d) { p32(d, r_off, o.sekcje[indeks_text].va); }},
        {"relokacja poza obrazem", [&](auto& d) { p32(d, r_off, o.rozmiar_obrazu + 0x1000); }},
        {"SizeOfBlock niepoprawny", [&](auto& d) { p32(d, r_off + 4, 6); }},
        {"tablica symboli COFF", [&](auto& d) { p32(d, coff + 8, 0x200); p32(d, coff + 12, 1); }},
        {"nazwa sekcji obcieta (.rodata.)", [&](auto& d) { std::memcpy(&d[tab + indeks_rdata * 40u], ".rodata.", 8); }},
    };
    int przepuszczone = 0;
    for (const Mutacja& m : lista) {
        std::vector<uint8_t> kopia = wzor;
        m.f(kopia);
        const Wynik w = waliduj(kopia, nullptr, -1, -1);
        std::printf("[T-PE-MUT] %-46s -> %s%s%s\n", m.nazwa, w.bledy.empty() ? "PRZYJETA (BLAD TESTU)" : "odrzucona: ",
                    w.bledy.empty() ? "" : w.bledy.front().c_str(), "");
        if (w.bledy.empty()) ++przepuszczone;
    }
    // Wskaznik relokowany wskazujacy poza obraz (modyfikacja danych w miejscu DIR64).
    {
        std::vector<uint8_t> kopia = wzor;
        const uint16_t wpis = u16(kopia, r_off + 8);
        uint64_t off = 0;
        rva_na_plik(o, u32(kopia, r_off) + (wpis & 0xFFF), 8, &off);
        for (int i = 0; i < 8; ++i) kopia[off + i] = 0xEE;
        const Wynik w = waliduj(kopia, nullptr, -1, -1);
        std::printf("[T-PE-MUT] %-46s -> %s\n", "wskaznik DIR64 poza obrazem", w.bledy.empty() ? "PRZYJETA (BLAD TESTU)" : "odrzucona");
        if (w.bledy.empty()) ++przepuszczone;
    }
    std::printf("[T-PE-MUT] mutacje: %zu, przepuszczone: %d\n", lista.size() + 1, przepuszczone);
    return przepuszczone == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--mutacje") == 0) return mutacje(argv[2]);
    if (argc < 2) { std::fprintf(stderr, "uzycie: %s <obraz.efi> [--wejscie-rva RVA] [--relokacje-elf N]\n", argv[0]); return 2; }
    int64_t wejscie = -1, relokacje = -1;
    for (int i = 2; i + 1 < argc; i += 2) {
        if (std::strcmp(argv[i], "--wejscie-rva") == 0) wejscie = int64_t(std::strtoull(argv[i + 1], nullptr, 0));
        else if (std::strcmp(argv[i], "--relokacje-elf") == 0) relokacje = int64_t(std::strtoull(argv[i + 1], nullptr, 0));
        else { std::fprintf(stderr, "nieznana opcja %s\n", argv[i]); return 2; }
    }
    return raport(argv[1], wejscie, relokacje);
}
