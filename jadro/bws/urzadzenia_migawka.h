#pragma once
/*
 * Bursztyn OS - budowa odpowiedzi BWS 105 z wewnetrznych struktur jadra (tylko jadro i testy hosta).
 * Funkcje sa czyste: dostaja juz pobrane wskazniki/wartosci, niczego nie alokuja, nie loguja i nie
 * dotykaja sprzetu. Wywolujacy (bws_nowoczesne.cpp) trzyma krotka sekcje bez przerwan na czas
 * kopiowania licznikow, aby migawka byla spojna na jednym procesorze.
 */
#include "urzadzenia_telemetria.h"
#include "block_device.h"
#include "network_device.h"

namespace UrzadzeniaMigawka {

inline uint64_t czytaj(const uint64_t& c) { return __atomic_load_n(&c, __ATOMIC_RELAXED); }

/*
 * filtr  - urzadzenie rejestru blokowego (tozsamosc, pojemnosc, tryb tylko do odczytu),
 * sprzet - urzadzenie sterownika pod filtrem (liczniki BlockIoStats); nullptr = brak sterownika.
 */
inline void dysk(BwsDyskInfoV1& o, uint32_t id, const BlockDevice& filtr, const BlockDevice* sprzet, bool systemowy) {
    o = BwsDyskInfoV1{};
    o.id = id;
    const BlockDevice& z = sprzet ? *sprzet : filtr;
    switch (z.device_type) {
        case BlockDeviceType::AHCI: o.typ = BWS_DYSK_AHCI; break;
        case BlockDeviceType::VirtIO: o.typ = BWS_DYSK_VIRTIO; break;
        case BlockDeviceType::NVMe: o.typ = BWS_DYSK_NVME; break;
        default: o.typ = 0; break;
    }
    o.rozmiar_sektora = z.logical_sector_size;
    o.sektory = z.total_sectors;
    if (sprzet && sprzet->total_sectors && sprzet->can_read()) o.flagi |= BWS_DYSK_ONLINE;
    if (z.read_only || !z.writable) o.flagi |= BWS_DYSK_TYLKO_ODCZYT;
    if (z.removable) o.flagi |= BWS_DYSK_WYMIENNY;
    if (systemowy) o.flagi |= BWS_DYSK_SYSTEMOWY;
    /* Model tylko z IDENTIFY (AHCI). VirtIO-BLK ma w polu model staly napis sterownika, nie model urzadzenia. */
    if (z.device_type == BlockDeviceType::AHCI && z.model[0]) {
        UrzadzeniaTelemetria::kopiuj_nazwe(o.model, sizeof(o.model), z.model, sizeof(z.model));
        if (o.model[0]) o.flagi |= BWS_DYSK_MODEL_URZADZENIA;
    }
    if (!sprzet) return;
    const BlockIoStats& s = sprzet->io;
    o.bajty_odczytane = czytaj(s.read_bytes);
    o.bajty_zapisane = czytaj(s.write_bytes);
    o.operacje_odczytu = czytaj(s.read_ops);
    o.operacje_zapisu = czytaj(s.write_ops);
    o.bledy_odczytu = czytaj(s.read_errors);
    o.bledy_zapisu = czytaj(s.write_errors);
    o.czas_odczytu_ns = czytaj(s.read_ns);
    o.czas_zapisu_ns = czytaj(s.write_ns);
    const uint64_t a = czytaj(s.timed_read_ops), b = czytaj(s.timed_write_ops);
    o.operacje_z_czasem = a > UINT64_MAX - b ? UINT64_MAX : a + b;
    o.oprozniania = czytaj(s.flushes);
    o.bledy_oprozniania = czytaj(s.flush_errors);
}

/* lacze = wynik get_link_state() (pobrany przez wywolujacego). Prednosc lacza nie jest raportowana przez sterowniki. */
inline void siec(BwsSiecInfoV1& o, const NetworkDevice& d, bool glowna, NetLinkState lacze) {
    o = BwsSiecInfoV1{};
    o.id = d.registration_id;
    o.typ = d.device_type == NetDeviceType::E1000 ? BWS_SIEC_E1000 : d.device_type == NetDeviceType::VirtIO ? BWS_SIEC_VIRTIO : 0;
    o.mtu = d.mtu;
    if (d.active) o.flagi |= BWS_SIEC_AKTYWNA;
    if (glowna) o.flagi |= BWS_SIEC_GLOWNA;
    if (lacze != NetLinkState::Unknown) {
        o.flagi |= BWS_SIEC_LACZE_ZNANE;
        if (lacze == NetLinkState::Up) o.flagi |= BWS_SIEC_LACZE;
    }
    if (net_mac_valid(d.mac)) {
        o.flagi |= BWS_SIEC_MAC;
        for (int i = 0; i < 6; ++i) o.mac[i] = d.mac[i];
    }
    UrzadzeniaTelemetria::kopiuj_nazwe(o.nazwa, sizeof(o.nazwa), d.name, sizeof(d.name));
    /* Kanoniczne liczniki warstwy NetworkDevice: RX w net_receive_frame, TX w transmit sterownika po zakonczeniu
     * deskryptora. Nie uzywamy get_stats(): e1000 nadpisuje tam czesc pol licznikami sterownika. */
    const NetDeviceStats& s = d.stats;
    o.rx_bajty = czytaj(s.rx_bytes);
    o.rx_pakiety = czytaj(s.rx_packets);
    o.rx_bledy = czytaj(s.rx_errors);
    o.rx_odrzucone = czytaj(s.rx_dropped);
    o.tx_bajty = czytaj(s.tx_bytes);
    o.tx_pakiety = czytaj(s.tx_packets);
    o.tx_bledy = czytaj(s.tx_errors);
    o.tx_odrzucone = czytaj(s.tx_dropped);
}

/* Wartosci zebrane przez jadro (grafika.cpp, graphics_perf.h, virtio_gpu.h, skladacz_obrazu). */
struct ZrodloGrafiki {
    uint32_t backend = 0, szerokosc = 0, wysokosc = 0, bpp = 0, pitch = 0;
    bool kursor_sprzetowy = false, zegar = false;
    bool virtio = false, blad_trwaly = false;
    uint16_t pci_vendor = 0, pci_device = 0;
    uint32_t format_virtio = 0;
    uint64_t bledy_gpu = 0, spoznione_gpu = 0;
    /* GraphicsPerf (kolejnosc pol jak w BwsGrafikaInfoV1) */
    uint64_t klatki = 0, pelne_klatki = 0, czas_skladania_ns = 0, czas_prezentacji_ns = 0, piksele = 0;
    uint64_t kopie_bajty = 0, czas_kopii_ns = 0, polecenia_gpu = 0, czas_oczekiwania_gpu_ns = 0;
    uint64_t transfery = 0, piksele_transferu = 0, flush = 0, piksele_flush = 0, kursor = 0;
    uint64_t warstwy_bajtow = 0, bufory_bajtow = 0;
};

inline void grafika(BwsGrafikaInfoV1& o, const ZrodloGrafiki& z) {
    o = BwsGrafikaInfoV1{};
    o.backend = z.backend;
    o.szerokosc = z.szerokosc; o.wysokosc = z.wysokosc; o.bpp = z.bpp; o.pitch = z.pitch;
    if (z.kursor_sprzetowy) o.flagi |= BWS_GRAF_KURSOR_SPRZETOWY;
    if (z.zegar) o.flagi |= BWS_GRAF_ZEGAR;
    o.klatki = z.klatki; o.pelne_klatki = z.pelne_klatki;
    o.czas_skladania_ns = z.zegar ? z.czas_skladania_ns : 0;
    o.czas_prezentacji_ns = z.zegar ? z.czas_prezentacji_ns : 0;
    o.piksele_skladane = z.piksele;
    o.kopie_bajty = z.kopie_bajty; o.czas_kopii_ns = z.zegar ? z.czas_kopii_ns : 0;
    o.prezentacje_kursora = z.kursor;
    o.pamiec_warstw_bajtow = z.warstwy_bajtow;
    o.pamiec_buforow_bajtow = z.bufory_bajtow;
    /* Liczniki VirtIO-GPU maja znaczenie tylko, gdy to VirtIO-GPU jest aktywnym backendem. */
    if (z.virtio && z.backend == 4U) {
        o.flagi |= BWS_GRAF_LICZNIKI_VIRTIO;
        if (z.pci_vendor) { o.flagi |= BWS_GRAF_PCI; o.pci_vendor = z.pci_vendor; o.pci_device = z.pci_device; }
        if (z.blad_trwaly) o.flagi |= BWS_GRAF_BLAD_TRWALY;
        o.format_virtio = z.format_virtio;
        o.polecenia_gpu = z.polecenia_gpu;
        o.czas_oczekiwania_gpu_ns = z.zegar ? z.czas_oczekiwania_gpu_ns : 0;
        o.bledy_gpu = z.bledy_gpu; o.spoznione_gpu = z.spoznione_gpu;
        o.transfery = z.transfery; o.piksele_transferu = z.piksele_transferu;
        o.flush = z.flush; o.piksele_flush = z.piksele_flush;
    }
}

}  // namespace UrzadzeniaMigawka
