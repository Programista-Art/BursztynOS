#pragma once
/*
 * Menedzer zadan - model urzadzen (BWS 105) bez GUI, bez alokacji, bez libc.
 *
 * Jadro podaje skumulowane liczniki; tempo = roznica dwoch migawek / roznica czas_ns (zegar monotoniczny).
 * Urzadzenia sa dopasowywane po stalym id (dysk: numer rejestracji blokowej, siec: numer rejestracji
 * interfejsu). Brak poprzedniej migawki, nowe id, spadek licznika (reinicjalizacja sterownika) albo
 * zerowy/ujemny przedzial = tempo niedostepne w tym przedziale - nigdy "0 B/s".
 */
#include "model.h"
#include "urzadzenia_telemetria.h"

namespace MenedzerZadan {

/* Historia o stalej pojemnosci (bufor kolowy). */
struct Historia {
    int v[HISTORIA]{};
    size_t glowa = 0, n = 0;
    void dodaj(int x) { v[glowa] = x; glowa = (glowa + 1) % HISTORIA; if (n < HISTORIA) ++n; }
    size_t kopiuj(int* out, size_t cap) const {
        const size_t k = n < cap ? n : cap;
        for (size_t i = 0; i < k; ++i) out[i] = v[(glowa + HISTORIA - k + i) % HISTORIA];
        return k;
    }
    void wyczysc() { glowa = n = 0; }
};

/* Ile jednostek na sekunde: delta * 1e9 / dt bez dzielenia 128-bitowego (Ring 3 freestanding). */
inline bool na_sekunde(uint64_t teraz, uint64_t przed, uint64_t dt_ns, uint64_t* out) {
    if (!out || dt_ns == 0 || teraz < przed) return false;
    constexpr uint64_t NS = 1000000000ULL;
    const uint64_t d = teraz - przed, q = d / dt_ns;
    uint64_t r = d % dt_ns, t = dt_ns;
    while (r > UINT64_MAX / NS) { r >>= 1; t >>= 1; }        // tylko przy przedziale > ~18 s
    if (q > (UINT64_MAX - NS) / NS) { *out = UINT64_MAX; return true; }
    *out = q * NS + (t ? (r * NS + t / 2) / t : 0);
    return true;
}

inline void format_tempa(uint64_t bajtow_na_s, char* out, size_t cap) {
    Tekst t(out, cap);
    if (bajtow_na_s < 1024) { t.liczba(bajtow_na_s); t.dodaj(" B/s"); return; }
    if (bajtow_na_s < 1024ULL * 1024) { t.dziesietna((bajtow_na_s * 10 + 512) / 1024); t.dodaj(" KiB/s"); return; }
    if (bajtow_na_s < 1024ULL * 1024 * 1024) { t.dziesietna((bajtow_na_s * 10 + 512 * 1024) / (1024 * 1024)); t.dodaj(" MiB/s"); return; }
    t.dziesietna((bajtow_na_s / 1024 * 10 + 512 * 1024) / (1024 * 1024)); t.dodaj(" GiB/s");
}

inline void format_ilosci(uint64_t bajty, char* out, size_t cap) {
    Tekst t(out, cap);
    if (bajty < 1024) { t.liczba(bajty); t.dodaj(" B"); return; }
    if (bajty < 1024ULL * 1024) { t.dziesietna((bajty * 10 + 512) / 1024); t.dodaj(" KiB"); return; }
    format_pamieci(bajty, out, cap);
}

/* KiB/s dla wykresu (zaokraglenie w gore, zeby niezerowy ruch byl widoczny). */
inline int kib_na_s(uint64_t b) { const uint64_t k = (b + 1023) / 1024; return k > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int>(k); }

struct Dysk {
    BwsDyskInfoV1 info{};
    bool tempo = false;              // tempa ponizej sa wazne w ostatnim przedziale
    uint64_t odczyt_bps = 0, zapis_bps = 0;
    uint64_t odczyty_ps = 0, zapisy_ps = 0;      // polecenia na sekunde
    bool czas = false;               // kazde polecenie przedzialu mialo pomiar czasu
    int czas_io_promile = NIEDOSTEPNE;           // ms I/O na sekunde (x1/1000): suma czasu polecen / przedzial
    uint64_t sredni_czas_us = 0;     // na polecenie w przedziale (gdy byly polecenia)
    uint64_t polecenia_przedzialu = 0;
    Historia h_odczyt, h_zapis;      // KiB/s
};

struct Siec {
    BwsSiecInfoV1 info{};
    bool tempo = false;
    uint64_t rx_bps = 0, tx_bps = 0, rx_pps = 0, tx_pps = 0;
    Historia h_rx, h_tx;             // KiB/s
};

struct Grafika {
    BwsGrafikaInfoV1 info{};
    bool tempo = false;
    uint64_t klatki_10ps = 0;        // klatki na sekunde x10
    int skladanie_promile = NIEDOSTEPNE;        // czas CPU kompozytora (skladanie) / przedzial
    int prezentacja_promile = NIEDOSTEPNE;      // czas przekazania klatki do urzadzenia / przedzial
    uint64_t transfer_bps = 0;       // VirtIO-GPU: piksele TRANSFER_TO_HOST * 4 B, albo kopie do framebuffera
    uint64_t polecenia_ps = 0;
    Historia h_klatki;               // klatki/s
};

class ModelUrzadzen {
public:
    /* false = odpowiedz niepoprawna: dane poprzednie zostaja, przedzial zaczyna sie od nowa. */
    bool zastosuj(const BwsUrzadzeniaOdpowiedz& o) {
        const auto& n = o.naglowek;
        if (n.wersja != BWS_URZADZENIA_V1_ABI || n.rozmiar_naglowka != sizeof(BwsUrzadzeniaNaglowek) ||
            n.rozmiar_dysku != sizeof(BwsDyskInfoV1) || n.rozmiar_sieci != sizeof(BwsSiecInfoV1) ||
            n.rozmiar_grafiki != sizeof(BwsGrafikaInfoV1) || n.liczba_dyskow > BWS_URZ_MAKS_DYSKOW ||
            n.liczba_sieci > BWS_URZ_MAKS_SIECI || n.liczba_grafiki > BWS_URZ_MAKS_GRAFIKI) { blad(); return false; }
        for (uint32_t i = 0; i < n.liczba_dyskow; ++i)
            for (uint32_t j = 0; j < i; ++j) if (o.dyski[i].id == o.dyski[j].id) { blad(); return false; }
        for (uint32_t i = 0; i < n.liczba_sieci; ++i)
            for (uint32_t j = 0; j < i; ++j) if (o.sieci[i].id == o.sieci[j].id) { blad(); return false; }

        const bool zegar = (n.flagi & BWS_URZ_ZEGAR) && n.czas_ns;
        const uint64_t dt = zegar && ma_poprzednia_ && n.czas_ns > poprz_ns_ ? n.czas_ns - poprz_ns_ : 0;

        Dysk nd[BWS_URZ_MAKS_DYSKOW];
        for (uint32_t i = 0; i < n.liczba_dyskow; ++i) {
            const BwsDyskInfoV1& w = o.dyski[i];
            Dysk& d = nd[i];
            const Dysk* s = znajdz_dysk(w.id);
            if (s) { d.h_odczyt = s->h_odczyt; d.h_zapis = s->h_zapis; }
            d.info = w;
            d.info.model[sizeof(d.info.model) - 1] = '\0';
            if (!s || !dt) continue;
            const BwsDyskInfoV1& p = s->info;
            uint64_t ops_o = 0, ops_z = 0, bl_o = 0, bl_z = 0;
            d.tempo = na_sekunde(w.bajty_odczytane, p.bajty_odczytane, dt, &d.odczyt_bps) &&
                      na_sekunde(w.bajty_zapisane, p.bajty_zapisane, dt, &d.zapis_bps) &&
                      na_sekunde(w.operacje_odczytu, p.operacje_odczytu, dt, &d.odczyty_ps) &&
                      na_sekunde(w.operacje_zapisu, p.operacje_zapisu, dt, &d.zapisy_ps) &&
                      w.bledy_odczytu >= p.bledy_odczytu && w.bledy_zapisu >= p.bledy_zapisu;
            if (!d.tempo) { d.odczyt_bps = d.zapis_bps = d.odczyty_ps = d.zapisy_ps = 0; continue; }
            ops_o = w.operacje_odczytu - p.operacje_odczytu; ops_z = w.operacje_zapisu - p.operacje_zapisu;
            bl_o = w.bledy_odczytu - p.bledy_odczytu; bl_z = w.bledy_zapisu - p.bledy_zapisu;
            d.polecenia_przedzialu = ops_o + ops_z + bl_o + bl_z;
            // Czas I/O tylko, gdy KAZDE polecenie przedzialu mialo pomiar (inaczej suma bylaby zanizona).
            if (w.operacje_z_czasem >= p.operacje_z_czasem && w.czas_odczytu_ns >= p.czas_odczytu_ns &&
                w.czas_zapisu_ns >= p.czas_zapisu_ns &&
                w.operacje_z_czasem - p.operacje_z_czasem == d.polecenia_przedzialu) {
                const uint64_t ns = (w.czas_odczytu_ns - p.czas_odczytu_ns) + (w.czas_zapisu_ns - p.czas_zapisu_ns);
                d.czas = true;
                d.czas_io_promile = promile(ns, dt);
                d.sredni_czas_us = d.polecenia_przedzialu ? ns / d.polecenia_przedzialu / 1000 : 0;
            }
            d.h_odczyt.dodaj(kib_na_s(d.odczyt_bps)); d.h_zapis.dodaj(kib_na_s(d.zapis_bps));
        }
        Siec ns_[BWS_URZ_MAKS_SIECI];
        for (uint32_t i = 0; i < n.liczba_sieci; ++i) {
            const BwsSiecInfoV1& w = o.sieci[i];
            Siec& d = ns_[i];
            const Siec* s = znajdz_siec(w.id);
            if (s) { d.h_rx = s->h_rx; d.h_tx = s->h_tx; }
            d.info = w;
            d.info.nazwa[sizeof(d.info.nazwa) - 1] = '\0';
            if (!s || !dt) continue;
            const BwsSiecInfoV1& p = s->info;
            d.tempo = na_sekunde(w.rx_bajty, p.rx_bajty, dt, &d.rx_bps) && na_sekunde(w.tx_bajty, p.tx_bajty, dt, &d.tx_bps) &&
                      na_sekunde(w.rx_pakiety, p.rx_pakiety, dt, &d.rx_pps) && na_sekunde(w.tx_pakiety, p.tx_pakiety, dt, &d.tx_pps);
            if (!d.tempo) { d.rx_bps = d.tx_bps = d.rx_pps = d.tx_pps = 0; continue; }
            d.h_rx.dodaj(kib_na_s(d.rx_bps)); d.h_tx.dodaj(kib_na_s(d.tx_bps));
        }
        Grafika ng{};
        const bool jest_grafika = n.liczba_grafiki > 0 && o.grafika[0].backend != 0;
        if (jest_grafika) {
            ng.h_klatki = grafika_.h_klatki;
            ng.info = o.grafika[0];
            const BwsGrafikaInfoV1& w = ng.info;
            const BwsGrafikaInfoV1& p = grafika_.info;
            uint64_t klatki = 0;
            if (dt && ma_grafike_ && p.backend == w.backend &&
                na_sekunde(w.klatki * 10, p.klatki * 10, dt, &klatki)) {
                ng.tempo = true;
                ng.klatki_10ps = klatki;
                if ((w.flagi & BWS_GRAF_ZEGAR) && w.czas_skladania_ns >= p.czas_skladania_ns)
                    ng.skladanie_promile = promile(w.czas_skladania_ns - p.czas_skladania_ns, dt);
                if ((w.flagi & BWS_GRAF_ZEGAR) && w.czas_prezentacji_ns >= p.czas_prezentacji_ns)
                    ng.prezentacja_promile = promile(w.czas_prezentacji_ns - p.czas_prezentacji_ns, dt);
                uint64_t px = 0;
                if (w.flagi & BWS_GRAF_LICZNIKI_VIRTIO) {
                    if (na_sekunde(w.piksele_transferu, p.piksele_transferu, dt, &px)) ng.transfer_bps = px * 4;
                    (void)na_sekunde(w.polecenia_gpu, p.polecenia_gpu, dt, &ng.polecenia_ps);
                } else (void)na_sekunde(w.kopie_bajty, p.kopie_bajty, dt, &ng.transfer_bps);
                ng.h_klatki.dodaj(static_cast<int>((klatki + 5) / 10));
            }
        }

        for (uint32_t i = 0; i < n.liczba_dyskow; ++i) dyski_[i] = nd[i];
        liczba_dyskow_ = n.liczba_dyskow;
        for (uint32_t i = 0; i < n.liczba_sieci; ++i) sieci_[i] = ns_[i];
        liczba_sieci_ = n.liczba_sieci;
        grafika_ = ng;
        ma_grafike_ = jest_grafika;
        naglowek_ = n;
        ma_poprzednia_ = zegar;
        poprz_ns_ = zegar ? n.czas_ns : 0;
        dt_ns_ = dt;
        ++probki_;
        return true;
    }
    void blad() { ma_poprzednia_ = false; ++bledy_; }

    uint32_t liczba_dyskow() const { return liczba_dyskow_; }
    uint32_t liczba_sieci() const { return liczba_sieci_; }
    const Dysk* dysk(uint32_t i) const { return i < liczba_dyskow_ ? &dyski_[i] : nullptr; }
    const Siec* siec(uint32_t i) const { return i < liczba_sieci_ ? &sieci_[i] : nullptr; }
    const Grafika* grafika() const { return ma_grafike_ ? &grafika_ : nullptr; }
    const BwsUrzadzeniaNaglowek& naglowek() const { return naglowek_; }
    const Dysk* znajdz_dysk(uint32_t id) const { for (uint32_t i = 0; i < liczba_dyskow_; ++i) if (dyski_[i].info.id == id) return &dyski_[i]; return nullptr; }
    const Siec* znajdz_siec(uint32_t id) const { for (uint32_t i = 0; i < liczba_sieci_; ++i) if (sieci_[i].info.id == id) return &sieci_[i]; return nullptr; }
    bool psf_w_ram() const { return (naglowek_.flagi & BWS_URZ_PSF_W_RAM) != 0; }
    uint64_t probki() const { return probki_; }
    uint64_t bledy() const { return bledy_; }
    uint64_t przedzial_ns() const { return dt_ns_; }

private:
    Dysk dyski_[BWS_URZ_MAKS_DYSKOW];
    Siec sieci_[BWS_URZ_MAKS_SIECI];
    Grafika grafika_{};
    uint32_t liczba_dyskow_ = 0, liczba_sieci_ = 0;
    bool ma_grafike_ = false, ma_poprzednia_ = false;
    BwsUrzadzeniaNaglowek naglowek_{};
    uint64_t poprz_ns_ = 0, dt_ns_ = 0, probki_ = 0, bledy_ = 0;
};

/* Nazwy (polskie, bez wymyslonych modeli). */
inline const char* nazwa_typu_dysku(uint32_t typ) {
    switch (typ) {
        case BWS_DYSK_AHCI: return "SATA/AHCI";
        case BWS_DYSK_VIRTIO: return "VirtIO Block";
        case BWS_DYSK_NVME: return "NVMe";
        default: return "Nieznany typ";
    }
}
inline const char* nazwa_sterownika_sieci(uint32_t typ) {
    switch (typ) {
        case BWS_SIEC_E1000: return "Intel E1000";
        case BWS_SIEC_VIRTIO: return "VirtIO-Net";
        default: return "Nieznany sterownik";
    }
}
inline const char* nazwa_backendu(uint32_t b) {
    switch (b) {
        case 1: return "VESA (framebuffer BIOS)";
        case 2: return "UEFI GOP (framebuffer firmware)";
        case 3: return "Bochs VBE";
        case 4: return "VirtIO-GPU 2D";
        default: return "Nieznany";
    }
}

}  // namespace MenedzerZadan
