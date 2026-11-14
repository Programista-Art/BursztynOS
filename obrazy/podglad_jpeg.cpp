#include "podglad_jpeg.h"

// Patrz podglad_jpeg.h. Kod jest przeznaczony dla Ring 3 (bez wyjatkow, bez libc) i kompiluje sie
// tez na hoscie (testy ASan/UBSan).
namespace {

constexpr uint32_t BUFOR_ODCZYTU = 32U * 1024U;
constexpr int LUT_BITY = 9;
constexpr uint8_t ZZ[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

// Wspolczynniki n-punktowego IDCT (z czynnikiem C(0)=1/sqrt2 w wierszu u=0).
constexpr float T2[2][2] = {{0.70710678f, 0.70710678f}, {0.70710678f, -0.70710678f}};
constexpr float T4[4][4] = {
    {0.70710678f, 0.70710678f, 0.70710678f, 0.70710678f},
    {0.92387953f, 0.38268343f, -0.38268343f, -0.92387953f},
    {0.70710678f, -0.70710678f, -0.70710678f, 0.70710678f},
    {0.38268343f, -0.92387953f, 0.92387953f, -0.38268343f}};

struct Huff {
    bool ok{false};
    int mincode[17]{};
    int maxcode[17]{};
    int valptr[17]{};
    uint8_t val[256]{};
    uint16_t lut[1 << LUT_BITY]{}; // (dlugosc << 8) | symbol, 0 = brak
};

struct Komp {
    uint8_t id{0}, h{0}, v{0}, tq{0};
    uint32_t bw{0}, bh{0}, pw{0}, ph{0};
    uint8_t* plane{nullptr};
    int pred{0};
    uint8_t td{0}, ta{0};
    bool dc_gotowe{false};
};

struct Czytnik {
    const PodgladZrodlo* src{nullptr};
    AlokujObraz al{nullptr};
    ZwolnijObraz zw{nullptr};
    uint8_t* buf{nullptr};
    uint32_t len{0}, pos{0};
    uint64_t off{0};
    bool koniec{false};

    bool otworz(const PodgladZrodlo* s, AlokujObraz a, ZwolnijObraz z) {
        src = s; al = a; zw = z; off = 0; len = pos = 0; koniec = false;
        buf = static_cast<uint8_t*>(a(BUFOR_ODCZYTU));
        return buf != nullptr;
    }
    void zamknij() { if (buf) zw(buf); buf = nullptr; }
    int bajt() {
        if (pos >= len) {
            if (koniec || off >= src->rozmiar) { koniec = true; return -1; }
            uint64_t zostalo = src->rozmiar - off;
            uint32_t chce = zostalo > BUFOR_ODCZYTU ? BUFOR_ODCZYTU : static_cast<uint32_t>(zostalo);
            uint32_t got = src->czytaj(src->ctx, off, buf, chce);
            if (got == 0 || got > chce) { koniec = true; return -1; }
            off += got; len = got; pos = 0;
        }
        return buf[pos++];
    }
    int u16() { int a = bajt(); int b = bajt(); return (a < 0 || b < 0) ? -1 : (a << 8) | b; }
    bool pomin(uint32_t n) { while (n--) if (bajt() < 0) return false; return true; }
};

bool zbuduj_huff(Huff& h, const uint8_t bits[17], const uint8_t* vals, int total) {
    h = Huff{};
    int code = 0, k = 0;
    for (int l = 1; l <= 16; ++l) {
        h.valptr[l] = k;
        h.mincode[l] = code;
        for (int i = 0; i < bits[l]; ++i) {
            if (k >= total || k >= 256) return false;
            if (l <= LUT_BITY) {
                const int baza = code << (LUT_BITY - l);
                for (int j = 0; j < (1 << (LUT_BITY - l)); ++j) {
                    if (baza + j >= (1 << LUT_BITY)) return false;
                    h.lut[baza + j] = static_cast<uint16_t>((l << 8) | vals[k]);
                }
            }
            ++code; ++k;
        }
        h.maxcode[l] = bits[l] ? code - 1 : -1;
        if (code > (1 << l)) return false;   // nadmiar kodow
        code <<= 1;
    }
    for (int i = 0; i < k; ++i) h.val[i] = vals[i];
    h.ok = true;
    return true;
}

} // namespace

struct PodgladJpeg {
    PodgladZrodlo src{};
    PodgladOpcje opt{};
    AlokujObraz al{nullptr};
    ZwolnijObraz zw{nullptr};
    Czytnik rd{};
    BladObrazu blad{BladObrazu::OK};
    PodgladWynik wynik{};

    uint16_t q[4][64]{};
    bool q_ok[4]{};
    Huff dc[4]{}, ac[4]{};
    uint32_t restart{0};
    int adobe{-1};
    bool sof{false}, progres{false};
    uint32_t w{0}, h{0};
    int nc{0};
    Komp k[3]{};
    uint32_t hmax{1}, vmax{1};
    uint32_t mcux{0}, mcuy{0};
    uint32_t n{1};   // rozmiar bloku wyjsciowego: 1, 2 albo 4
    bool planes_gotowe{false};

    // skan
    int pending_marker{-1};
    int scan_nc{0};
    int scan_idx[3]{};
    uint32_t ss{0}, se{0}, ah{0}, al_{0};
    uint32_t smcux{0}, smcuy{0}, scx{0}, scy{0}, rst_count{0};
    bool w_skanie{false};
    bool koniec_skanow{false};
    uint32_t skan_wiersze_gotowe{0}, skan_wiersze_razem{0}, wiersze_skanow_wykonane{0};
    uint32_t oy{0};   // nastepny wiersz wyjsciowy
    uint32_t ow{0}, oh{0};
    uint32_t* out{nullptr};
    uint64_t przydzial{0};

    // bity
    uint32_t acc{0};
    int nb{0};
    int nreal{0};
    bool trafiony{false};
    int marker{-1};

    bool przydziel_plane(Komp& c, uint64_t bajty) {
        c.plane = static_cast<uint8_t*>(al(static_cast<size_t>(bajty)));
        if (!c.plane) return false;
        for (uint64_t i = 0; i < bajty; ++i) c.plane[i] = 128;
        przydzial += bajty;
        if (przydzial > wynik.szczyt_bajtow) wynik.szczyt_bajtow = przydzial;
        return true;
    }

    // ---- bity -----------------------------------------------------------------
    void wyzeruj_bity() { acc = 0; nb = 0; nreal = 0; }
    void dopelnij() {
        while (nb <= 24) {
            uint32_t b = 0;
            bool rzeczywisty = false;
            if (!trafiony) {
                int c = rd.bajt();
                if (c < 0) { trafiony = true; marker = -2; }
                else if (c == 0xFF) {
                    int c2 = rd.bajt();
                    while (c2 == 0xFF) c2 = rd.bajt();
                    if (c2 == 0) { b = 0xFF; rzeczywisty = true; }
                    else if (c2 < 0) { trafiony = true; marker = -2; }
                    else { trafiony = true; marker = c2; }
                } else { b = static_cast<uint32_t>(c); rzeczywisty = true; }
            }
            acc |= b << (24 - nb);
            nb += 8;
            if (rzeczywisty) nreal += 8;
        }
    }
    uint32_t bity(int cnt) {  // 1..16
        if (nb < cnt) dopelnij();
        const uint32_t v = acc >> (32 - cnt);
        acc <<= cnt; nb -= cnt; nreal -= cnt;
        return v;
    }
    int dekoduj_huff(const Huff& hf) {
        if (nb < 16) dopelnij();
        const uint16_t e = hf.lut[acc >> (32 - LUT_BITY)];
        if (e) {
            const int l = e >> 8;
            acc <<= l; nb -= l; nreal -= l;
            return e & 0xFF;
        }
        int code = static_cast<int>(acc >> (32 - LUT_BITY));
        for (int l = LUT_BITY + 1; l <= 16; ++l) {
            code = static_cast<int>(acc >> (32 - l));
            if (hf.maxcode[l] >= 0 && code <= hf.maxcode[l] && code >= hf.mincode[l]) {
                acc <<= l; nb -= l; nreal -= l;
                return hf.val[hf.valptr[l] + code - hf.mincode[l]];
            }
        }
        return -1;
    }
    static int rozszerz(int v, int s) { return v < (1 << (s - 1)) ? v - (1 << s) + 1 : v; }

    // ---- naglowki -------------------------------------------------------------
    int nastepny_marker() {
        if (pending_marker >= 0) { const int m = pending_marker; pending_marker = -1; return m; }
        for (;;) {
            int c = rd.bajt();
            if (c < 0) return -1;
            if (c != 0xFF) continue;
            int c2 = rd.bajt();
            while (c2 == 0xFF) c2 = rd.bajt();
            if (c2 < 0) return -1;
            if (c2 != 0) return c2;
        }
    }

    bool segment_dqt(int len) {
        len -= 2;
        while (len > 0) {
            const int pq = rd.bajt();
            if (pq < 0) return false;
            const int prec = pq >> 4, id = pq & 15;
            if (prec > 1 || id > 3) return false;
            const int bajtow = prec ? 128 : 64;
            if (len < 1 + bajtow) return false;
            for (int i = 0; i < 64; ++i) {
                int v;
                if (prec) { v = rd.u16(); } else { v = rd.bajt(); }
                if (v <= 0 || v > 65535) return false;
                q[id][i] = static_cast<uint16_t>(v);
            }
            q_ok[id] = true;
            len -= 1 + bajtow;
        }
        return len == 0;
    }

    bool segment_dht(int len) {
        len -= 2;
        while (len > 0) {
            const int tc = rd.bajt();
            if (tc < 0 || len < 17) return false;
            const int klasa = tc >> 4, id = tc & 15;
            if (klasa > 1 || id > 3) return false;
            uint8_t bits[17] = {};
            int total = 0;
            for (int i = 1; i <= 16; ++i) {
                const int b = rd.bajt();
                if (b < 0) return false;
                bits[i] = static_cast<uint8_t>(b);
                total += b;
            }
            if (total > 256 || len < 17 + total) return false;
            uint8_t vals[256] = {};
            for (int i = 0; i < total; ++i) {
                const int b = rd.bajt();
                if (b < 0) return false;
                vals[i] = static_cast<uint8_t>(b);
            }
            if (!zbuduj_huff(klasa ? ac[id] : dc[id], bits, vals, total)) return false;
            len -= 17 + total;
        }
        return len == 0;
    }

    BladObrazu segment_sof(int len, int marker_sof) {
        if (sof || len < 8) return BladObrazu::Uszkodzony;
        if (marker_sof == 0xC2) progres = true;
        const int prec = rd.bajt();
        const int hh = rd.u16(), ww = rd.u16();
        const int ncc = rd.bajt();
        if (prec != 8) return BladObrazu::Nieobslugiwany;
        if (hh <= 0 || ww <= 0) return BladObrazu::Uszkodzony;
        if (ncc != 1 && ncc != 3) return BladObrazu::Nieobslugiwany;
        if (len != 8 + 3 * ncc) return BladObrazu::Uszkodzony;
        w = static_cast<uint32_t>(ww); h = static_cast<uint32_t>(hh); nc = ncc;
        hmax = vmax = 1;
        for (int i = 0; i < nc; ++i) {
            const int id = rd.bajt(), hv = rd.bajt(), tq = rd.bajt();
            if (id < 0 || hv < 0 || tq < 0) return BladObrazu::Uszkodzony;
            const int hs = hv >> 4, vs = hv & 15;
            if (hs < 1 || hs > 4 || vs < 1 || vs > 4 || tq > 3) return BladObrazu::Uszkodzony;
            for (int j = 0; j < i; ++j) if (k[j].id == id) return BladObrazu::Uszkodzony;
            k[i].id = static_cast<uint8_t>(id); k[i].h = static_cast<uint8_t>(hs);
            k[i].v = static_cast<uint8_t>(vs); k[i].tq = static_cast<uint8_t>(tq);
            if (static_cast<uint32_t>(hs) > hmax) hmax = static_cast<uint32_t>(hs);
            if (static_cast<uint32_t>(vs) > vmax) vmax = static_cast<uint32_t>(vs);
        }
        for (int i = 0; i < nc; ++i) {          // dzielniki musza byc calkowite (brak niestandardowych proporcji)
            if (hmax % k[i].h || vmax % k[i].v) return BladObrazu::Nieobslugiwany;
        }
        if (w > opt.maks_bok_zrodla || h > opt.maks_bok_zrodla ||
            static_cast<uint64_t>(w) * h > opt.maks_pikseli_zrodla) return BladObrazu::ZaDuzy;
        sof = true;
        wynik.szer_zrodla = w; wynik.wys_zrodla = h; wynik.progresywny = progres;
        mcux = (w + 8 * hmax - 1) / (8 * hmax);
        mcuy = (h + 8 * vmax - 1) / (8 * vmax);
        for (int i = 0; i < nc; ++i) { k[i].bw = mcux * k[i].h; k[i].bh = mcuy * k[i].v; }
        return BladObrazu::OK;
    }

    // Czyta segmenty do SOS (konfiguruje skan) albo do EOI/konca. true = skan gotowy.
    BladObrazu nastepny_skan(bool* jest_skan) {
        *jest_skan = false;
        for (;;) {
            const int m = nastepny_marker();
            if (m < 0 || m == 0xD9) { koniec_skanow = true; return BladObrazu::OK; }
            if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) continue;
            const int len = rd.u16();
            if (len < 2) return BladObrazu::Uszkodzony;
            if (m == 0xDB) { if (!segment_dqt(len)) return BladObrazu::Uszkodzony; }
            else if (m == 0xC4) { if (!segment_dht(len)) return BladObrazu::Uszkodzony; }
            else if (m == 0xC0 || m == 0xC1 || m == 0xC2) {
                const BladObrazu b = segment_sof(len, m);
                if (b != BladObrazu::OK) return b;
            }
            else if ((m >= 0xC3 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC)) return BladObrazu::Nieobslugiwany;
            else if (m == 0xDD) {
                if (len != 4) return BladObrazu::Uszkodzony;
                const int r = rd.u16();
                if (r < 0) return BladObrazu::Uszkodzony;
                restart = static_cast<uint32_t>(r);
            }
            else if (m == 0xEE && len >= 14) {   // Adobe APP14
                uint8_t t[12] = {};
                for (int i = 0; i < 12; ++i) { const int b = rd.bajt(); if (b < 0) return BladObrazu::Uszkodzony; t[i] = static_cast<uint8_t>(b); }
                if (t[0] == 'A' && t[1] == 'd' && t[2] == 'o' && t[3] == 'b' && t[4] == 'e') adobe = t[11];
                if (!rd.pomin(static_cast<uint32_t>(len - 14))) return BladObrazu::Uszkodzony;
            }
            else if (m == 0xDA) {
                if (!sof) return BladObrazu::Uszkodzony;
                const int ns = rd.bajt();
                if (ns < 1 || ns > nc || len != 6 + 2 * ns) return BladObrazu::Uszkodzony;
                scan_nc = ns;
                for (int i = 0; i < ns; ++i) {
                    const int cs = rd.bajt(), t = rd.bajt();
                    if (cs < 0 || t < 0) return BladObrazu::Uszkodzony;
                    int idx = -1;
                    for (int j = 0; j < nc; ++j) if (k[j].id == cs) idx = j;
                    if (idx < 0) return BladObrazu::Uszkodzony;
                    for (int j = 0; j < i; ++j) if (scan_idx[j] == idx) return BladObrazu::Uszkodzony;
                    scan_idx[i] = idx;
                    k[idx].td = static_cast<uint8_t>(t >> 4); k[idx].ta = static_cast<uint8_t>(t & 15);
                    if (k[idx].td > 3 || k[idx].ta > 3) return BladObrazu::Uszkodzony;
                }
                const int a = rd.bajt(), b = rd.bajt(), c = rd.bajt();
                if (a < 0 || b < 0 || c < 0) return BladObrazu::Uszkodzony;
                ss = static_cast<uint32_t>(a); se = static_cast<uint32_t>(b);
                ah = static_cast<uint32_t>(c >> 4); al_ = static_cast<uint32_t>(c & 15);
                *jest_skan = true;
                return BladObrazu::OK;
            }
            else if (!rd.pomin(static_cast<uint32_t>(len - 2))) return BladObrazu::Uszkodzony;
        }
    }

    // Dobor skali i alokacja plaszczyzn + powierzchni.
    BladObrazu przygotuj_pamiec() {
        static const uint32_t kandydaci[3] = {4, 2, 1};   // n => skala 8/n
        for (uint32_t cand : kandydaci) {
            if (progres && cand != 1) continue;
            const uint64_t opx = static_cast<uint64_t>((w * cand + 7) / 8) * ((h * cand + 7) / 8);
            if (opx > opt.maks_pikseli_podgladu) continue;
            uint64_t razem = opx * 4;
            for (int i = 0; i < nc; ++i) razem += static_cast<uint64_t>(k[i].bw * cand) * (k[i].bh * cand);
            if (razem > opt.budzet_bajtow) continue;
            n = cand;
            ow = (w * n + 7) / 8; oh = (h * n + 7) / 8;
            wynik.szer = ow; wynik.wys = oh; wynik.skala = 8 / n;
            for (int i = 0; i < nc; ++i) {
                k[i].pw = k[i].bw * n; k[i].ph = k[i].bh * n;
                if (!przydziel_plane(k[i], static_cast<uint64_t>(k[i].pw) * k[i].ph)) return BladObrazu::Pamiec;
            }
            out = static_cast<uint32_t*>(al(static_cast<size_t>(opx * 4)));
            if (!out) return BladObrazu::Pamiec;
            przydzial += opx * 4;
            if (przydzial > wynik.szczyt_bajtow) wynik.szczyt_bajtow = przydzial;
            wynik.bgra = out;
            planes_gotowe = true;
            return BladObrazu::OK;
        }
        return BladObrazu::ZaDuzy;
    }

    // ---- skan -----------------------------------------------------------------
    BladObrazu zacznij_skan() {
        // Walidacja parametrow skanu.
        if (progres) {
            if (ss != 0 || se != 0 || ah != 0) { w_skanie = false; return BladObrazu::OK; } // nie-DC => koniec
            if (al_ > 13) return BladObrazu::Uszkodzony;
        } else {
            if (ss != 0 || se != 63 || ah != 0 || al_ != 0) return BladObrazu::Nieobslugiwany;
        }
        for (int i = 0; i < scan_nc; ++i) {
            Komp& c = k[scan_idx[i]];
            if (!dc[c.td].ok) return BladObrazu::Uszkodzony;
            if (!progres && !ac[c.ta].ok) return BladObrazu::Uszkodzony;
            if (!q_ok[c.tq]) return BladObrazu::Uszkodzony;
            c.pred = 0;
        }
        if (!planes_gotowe) { const BladObrazu b = przygotuj_pamiec(); if (b != BladObrazu::OK) return b; }
        if (scan_nc == 1) {
            const Komp& c = k[scan_idx[0]];
            // Nieprzeplatany: bloki skladowej w jej wlasnej siatce.
            const uint32_t cw = (w * c.h + hmax - 1) / hmax, ch = (h * c.v + vmax - 1) / vmax;
            smcux = (cw + 7) / 8; smcuy = (ch + 7) / 8;
            if (nc == 1) { smcux = mcux; smcuy = mcuy; }
        } else { smcux = mcux; smcuy = mcuy; }
        if (smcux > 0xFFFFFF || smcuy > 0xFFFFFF) return BladObrazu::ZaDuzy;
        scy = 0; scx = 0; rst_count = 0; skan_wiersze_gotowe = 0; skan_wiersze_razem = smcuy;
        wyzeruj_bity(); trafiony = false; marker = -1;
        w_skanie = true;
        return BladObrazu::OK;
    }

    bool restart_tak() {
        wyzeruj_bity();
        int m = marker;
        if (!trafiony) {
            // Szukaj znacznika RSTn w strumieniu.
            for (int guard = 0; guard < 4096; ++guard) {
                int c = rd.bajt();
                if (c < 0) return false;
                if (c != 0xFF) continue;
                int c2 = rd.bajt();
                while (c2 == 0xFF) c2 = rd.bajt();
                if (c2 < 0) return false;
                if (c2 != 0) { m = c2; break; }
            }
        }
        if (m < 0xD0 || m > 0xD7) return false;
        trafiony = false; marker = -1;
        for (int i = 0; i < scan_nc; ++i) k[scan_idx[i]].pred = 0;
        rst_count = 0;
        return true;
    }

    void wypisz_blok(Komp& c, uint32_t bx, uint32_t by, const int* coef /*natural 8x8 po dekwantyzacji w F*/) {
        if (bx >= c.bw || by >= c.bh) return;
        uint8_t* p = c.plane + static_cast<uint64_t>(by * n) * c.pw + static_cast<uint64_t>(bx) * n;
        if (n == 1) {
            float v = 128.0f + static_cast<float>(coef[0]) * 0.125f;
            p[0] = klamp(v);
            return;
        }
        float F[4][4];
        for (uint32_t r = 0; r < n; ++r) for (uint32_t cc = 0; cc < n; ++cc) F[r][cc] = static_cast<float>(coef[r * 8 + cc]);
        float tmp[4][4];
        for (uint32_t r = 0; r < n; ++r)
            for (uint32_t x = 0; x < n; ++x) {
                float s = 0;
                for (uint32_t u = 0; u < n; ++u) s += (n == 2 ? T2[u][x] : T4[u][x]) * F[r][u];
                tmp[r][x] = s;
            }
        for (uint32_t y = 0; y < n; ++y)
            for (uint32_t x = 0; x < n; ++x) {
                float s = 0;
                for (uint32_t v = 0; v < n; ++v) s += (n == 2 ? T2[v][y] : T4[v][y]) * tmp[v][x];
                p[static_cast<uint64_t>(y) * c.pw + x] = klamp(128.0f + 0.25f * s);
            }
    }
    static uint8_t klamp(float v) { return v <= 0.0f ? 0 : (v >= 255.0f ? 255 : static_cast<uint8_t>(v + 0.5f)); }

    // Dekoduje jeden blok. Zwraca false przy bledzie.
    bool blok(Komp& c, uint32_t bx, uint32_t by) {
        int coef[64];
        if (n > 1) for (int i = 0; i < 64; ++i) coef[i] = 0;
        const uint16_t* qt = q[c.tq];
        int s = dekoduj_huff(dc[c.td]);
        if (s < 0 || s > 15) return false;
        int diff = 0;
        if (s) diff = rozszerz(static_cast<int>(bity(s)), s);
        c.pred += diff;
        if (c.pred > (1 << 20) || c.pred < -(1 << 20)) return false;
        const int dcv = progres ? (c.pred * (1 << al_)) * qt[0] : c.pred * qt[0];
        coef[0] = dcv;
        if (!progres) {
            for (int kk = 1; kk < 64;) {
                const int rs = dekoduj_huff(ac[c.ta]);
                if (rs < 0) return false;
                const int r = rs >> 4, sz = rs & 15;
                if (!sz) { if (r == 15) { kk += 16; continue; } break; }
                kk += r;
                if (kk > 63) return false;
                const int v = rozszerz(static_cast<int>(bity(sz)), sz);
                if (n > 1) {
                    const int nat = ZZ[kk];
                    if (static_cast<uint32_t>(nat >> 3) < n && static_cast<uint32_t>(nat & 7) < n) coef[nat] = v * qt[kk];
                }
                ++kk;
            }
        }
        if (nreal < 0) return false;   // odczytano poza danymi skanu
        wypisz_blok(c, bx, by, coef);
        return true;
    }

    // Jeden wiersz MCU. Zwraca false przy bledzie.
    bool wiersz_mcu() {
        for (scx = 0; scx < smcux; ++scx) {
            if (restart && rst_count == restart) { if (!restart_tak()) return false; }
            if (scan_nc == 1 && nc > 1) {
                Komp& c = k[scan_idx[0]];
                if (!blok(c, scx, scy)) return false;
            } else {
                for (int i = 0; i < scan_nc; ++i) {
                    Komp& c = k[scan_idx[i]];
                    const uint32_t ch = (scan_nc == 1) ? 1 : c.h, cv = (scan_nc == 1) ? 1 : c.v;
                    for (uint32_t vy = 0; vy < cv; ++vy)
                        for (uint32_t hx = 0; hx < ch; ++hx)
                            if (!blok(c, scx * ch + hx, scy * cv + vy)) return false;
                }
            }
            ++rst_count;
        }
        return true;
    }

    void zakoncz_skan() {
        w_skanie = false;
        wyzeruj_bity();
        if (trafiony && marker >= 0) pending_marker = marker;
        trafiony = false; marker = -1;
        if (progres) for (int i = 0; i < scan_nc; ++i) k[scan_idx[i]].dc_gotowe = true;
        ++wiersze_skanow_wykonane;
    }

    bool wszystkie_dc() const { for (int i = 0; i < nc; ++i) if (!k[i].dc_gotowe) return false; return true; }

    // ---- konwersja ------------------------------------------------------------
    void wiersz_wyjscia(uint32_t y) {
        uint32_t* dst = out + static_cast<uint64_t>(y) * ow;
        const Komp& Y = k[0];
        const uint8_t* py = Y.plane + static_cast<uint64_t>(y < Y.ph ? y : Y.ph - 1) * Y.pw;
        if (nc == 1) {
            for (uint32_t x = 0; x < ow; ++x) { const uint32_t v = py[x < Y.pw ? x : Y.pw - 1]; dst[x] = 0xFF000000u | (v << 16) | (v << 8) | v; }
            return;
        }
        const Komp& B = k[1];
        const Komp& R = k[2];
        const uint32_t by_ = (y * B.v / vmax); const uint32_t ry_ = (y * R.v / vmax);
        const uint8_t* pb = B.plane + static_cast<uint64_t>(by_ < B.ph ? by_ : B.ph - 1) * B.pw;
        const uint8_t* pr = R.plane + static_cast<uint64_t>(ry_ < R.ph ? ry_ : R.ph - 1) * R.pw;
        const bool rgb = adobe == 0;
        for (uint32_t x = 0; x < ow; ++x) {
            const int yy = py[x < Y.pw ? x : Y.pw - 1];
            uint32_t bxx = x * B.h / hmax; if (bxx >= B.pw) bxx = B.pw - 1;
            uint32_t rxx = x * R.h / hmax; if (rxx >= R.pw) rxx = R.pw - 1;
            const int cb = pb[bxx], cr = pr[rxx];
            int r, g, b;
            if (rgb) { r = yy; g = cb; b = cr; }
            else {
                r = yy + ((91881 * (cr - 128)) >> 16);
                g = yy - ((22554 * (cb - 128) + 46802 * (cr - 128)) >> 16);
                b = yy + ((116130 * (cb - 128)) >> 16);
            }
            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);
            dst[x] = 0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
        }
    }

    void zwolnij_plaszczyzny() {
        for (int i = 0; i < 3; ++i) if (k[i].plane) { zw(k[i].plane); k[i].plane = nullptr; }
    }
};

// Bez placement new (Ring 3 jest freestanding): pamiec jest zerowana, potem ustawiane sa wartosci niezerowe.
static void inicjuj(PodgladJpeg* d) {
    uint8_t* b = reinterpret_cast<uint8_t*>(d);
    for (size_t i = 0; i < sizeof(PodgladJpeg); ++i) b[i] = 0;
    d->adobe = -1; d->pending_marker = -1; d->marker = -1; d->hmax = d->vmax = d->n = 1;
}

// ---- API -----------------------------------------------------------------------
PodgladJpeg* podglad_jpeg_otworz(const PodgladZrodlo& zrodlo, const PodgladOpcje& opcje,
                                 AlokujObraz alokuj, ZwolnijObraz zwolnij, BladObrazu* blad) {
    BladObrazu dummy;
    if (!blad) blad = &dummy;
    *blad = BladObrazu::Argument;
    if (!zrodlo.czytaj || !alokuj || !zwolnij || zrodlo.rozmiar < 4) return nullptr;
    PodgladJpeg* d = static_cast<PodgladJpeg*>(alokuj(sizeof(PodgladJpeg)));
    if (!d) { *blad = BladObrazu::Pamiec; return nullptr; }
    inicjuj(d);
    d->src = zrodlo; d->opt = opcje; d->al = alokuj; d->zw = zwolnij;
    if (!d->rd.otworz(&d->src, alokuj, zwolnij)) { *blad = BladObrazu::Pamiec; zwolnij(d); return nullptr; }
    const int b0 = d->rd.bajt(), b1 = d->rd.bajt();
    if (b0 != 0xFF || b1 != 0xD8) { *blad = BladObrazu::Format; d->rd.zamknij(); zwolnij(d); return nullptr; }
    // Czytaj naglowki do pierwszego SOS: SOF jest wtedy znany, a wymiary zwalidowane.
    bool jest = false;
    BladObrazu b = d->nastepny_skan(&jest);
    if (b == BladObrazu::OK && (!jest || !d->sof)) b = BladObrazu::Uszkodzony;
    if (b == BladObrazu::OK) b = d->zacznij_skan();
    if (b == BladObrazu::OK && !d->w_skanie) b = BladObrazu::Uszkodzony;  // pierwszy skan progresywny nie jest DC
    if (b != BladObrazu::OK) {
        *blad = b;
        d->zwolnij_plaszczyzny();
        if (d->out) zwolnij(d->out);
        d->rd.zamknij(); zwolnij(d);
        return nullptr;
    }
    *blad = BladObrazu::OK;
    return d;
}

PodgladStan podglad_jpeg_krok(PodgladJpeg* d, uint32_t maks) {
    if (!d || d->blad != BladObrazu::OK) return PodgladStan::Blad;
    uint32_t jednostki = maks ? maks : 1;
    while (jednostki) {
        if (d->w_skanie) {
            if (!d->wiersz_mcu()) { d->blad = BladObrazu::Uszkodzony; return PodgladStan::Blad; }
            ++d->scy; ++d->skan_wiersze_gotowe; --jednostki;
            if (d->scy >= d->smcuy) {
                d->zakoncz_skan();
                if (d->progres && d->wszystkie_dc()) d->koniec_skanow = true;
                else {
                    bool jest = false;
                    const BladObrazu b = d->nastepny_skan(&jest);
                    if (b != BladObrazu::OK) { d->blad = b; return PodgladStan::Blad; }
                    if (jest) {
                        const BladObrazu z = d->zacznij_skan();
                        if (z != BladObrazu::OK) { d->blad = z; return PodgladStan::Blad; }
                    }
                }
            }
            continue;
        }
        if (!d->koniec_skanow) {
            // Skan progresywny, ktory nie jest DC => dalsze skany pomijamy.
            if (d->progres) d->koniec_skanow = true;
            else {
                bool jest = false;
                const BladObrazu b = d->nastepny_skan(&jest);
                if (b != BladObrazu::OK) { d->blad = b; return PodgladStan::Blad; }
                if (jest) { const BladObrazu z = d->zacznij_skan(); if (z != BladObrazu::OK) { d->blad = z; return PodgladStan::Blad; } }
            }
            continue;
        }
        // Konwersja do BGRA.
        if (d->progres && !d->wszystkie_dc()) { d->blad = BladObrazu::Uszkodzony; return PodgladStan::Blad; }
        if (!d->progres && d->wiersze_skanow_wykonane == 0) { d->blad = BladObrazu::Uszkodzony; return PodgladStan::Blad; }
        if (d->oy >= d->oh) {
            d->zwolnij_plaszczyzny();
            d->przydzial = static_cast<uint64_t>(d->ow) * d->oh * 4;
            return PodgladStan::Gotowy;
        }
        d->wiersz_wyjscia(d->oy++);
        --jednostki;
    }
    if (d->koniec_skanow && d->oy >= d->oh) {
        d->zwolnij_plaszczyzny();
        return PodgladStan::Gotowy;
    }
    return PodgladStan::Dalej;
}

uint32_t podglad_jpeg_postep_promile(const PodgladJpeg* d) {
    if (!d) return 0;
    if (d->koniec_skanow) return d->oh ? 900U + static_cast<uint32_t>(100ULL * d->oy / d->oh) : 1000U;
    if (d->skan_wiersze_razem == 0) return 0;
    return static_cast<uint32_t>(900ULL * d->skan_wiersze_gotowe / d->skan_wiersze_razem);
}
const PodgladWynik& podglad_jpeg_wynik(const PodgladJpeg* d) { return d->wynik; }
BladObrazu podglad_jpeg_blad(const PodgladJpeg* d) { return d ? d->blad : BladObrazu::Argument; }

void podglad_jpeg_zamknij(PodgladJpeg* d, bool zachowaj) {
    if (!d) return;
    d->zwolnij_plaszczyzny();
    if (d->out && !zachowaj) d->zw(d->out);
    d->rd.zamknij();
    ZwolnijObraz zw = d->zw;
    zw(d);
}

BladObrazu podglad_jpeg_naglowek(const PodgladZrodlo& zrodlo, uint32_t* w, uint32_t* h, bool* progres,
                                 AlokujObraz alokuj, ZwolnijObraz zwolnij) {
    if (w) *w = 0;
    if (h) *h = 0;
    if (!zrodlo.czytaj || !alokuj || !zwolnij || zrodlo.rozmiar < 4) return BladObrazu::Argument;
    PodgladJpeg* d = static_cast<PodgladJpeg*>(alokuj(sizeof(PodgladJpeg)));
    if (!d) return BladObrazu::Pamiec;
    inicjuj(d);
    d->src = zrodlo; d->opt = {0, 0, 65535, 0xFFFFFFFFFFFFULL}; d->al = alokuj; d->zw = zwolnij;
    BladObrazu b = BladObrazu::Pamiec;
    if (d->rd.otworz(&d->src, alokuj, zwolnij)) {
        const int b0 = d->rd.bajt(), b1 = d->rd.bajt();
        if (b0 != 0xFF || b1 != 0xD8) b = BladObrazu::Format;
        else {
            bool jest = false;
            b = BladObrazu::OK;
            // Czytaj segmenty do SOF; skan nie jest potrzebny.
            for (;;) {
                const int m = d->nastepny_marker();
                if (m < 0 || m == 0xD9 || m == 0xDA) { b = d->sof ? BladObrazu::OK : BladObrazu::Uszkodzony; break; }
                if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) continue;
                const int len = d->rd.u16();
                if (len < 2) { b = BladObrazu::Uszkodzony; break; }
                if (m == 0xC0 || m == 0xC1 || m == 0xC2) {
                    b = d->segment_sof(len, m);
                    break;
                }
                if (m >= 0xC3 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) { b = BladObrazu::Nieobslugiwany; break; }
                if (!d->rd.pomin(static_cast<uint32_t>(len - 2))) { b = BladObrazu::Uszkodzony; break; }
            }
            (void)jest;
            if (b == BladObrazu::OK || b == BladObrazu::ZaDuzy) { if (w) *w = d->w; if (h) *h = d->h; if (progres) *progres = d->progres; }
        }
        d->rd.zamknij();
    }
    zwolnij(d);
    return b;
}
